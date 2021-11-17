#include <array>
#include <cstdlib>
#include <string>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

#include <STEPCAFControl_Reader.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_Material.hxx>
#include <XCAFDoc_Color.hxx>

#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_TreeNode.hxx>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <TopExp_Explorer.hxx>
#include <TopoDS_Builder.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_CompSolid.hxx>

#include <Quantity_PhysicalQuantity.hxx>
#include <Quantity_Color.hxx>

#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Wireframe.hxx>

#include "document.hpp"
#include "utils.hpp"

template <> struct fmt::formatter<TopAbs_ShapeEnum>: formatter<string_view> {
	// parse is inherited from formatter<string_view>.
	template <typename FormatContext>
	auto format(TopAbs_ShapeEnum c, FormatContext& ctx) {
		string_view name = "unknown";
		switch (c) {
		case TopAbs_COMPOUND: name = "COMPOUND"; break;
		case TopAbs_COMPSOLID: name = "COMPSOLID"; break;
		case TopAbs_SOLID: name = "SOLID"; break;
		case TopAbs_SHELL: name = "SHELL"; break;
		case TopAbs_FACE: name = "FACE"; break;
		case TopAbs_WIRE: name = "WIRE"; break;
		case TopAbs_EDGE: name = "EDGE"; break;
		case TopAbs_VERTEX: name = "VERTEX"; break;
		case TopAbs_SHAPE: name = "SHAPE"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};

static void
assign_cstring(std::string &dst, const TCollection_ExtendedString &src) {
	dst.resize(src.LengthOfCString());
	char * str = dst.data();
	size_t len = src.ToUTF8CString(str);

	if (len > dst.length()) {
		spdlog::critical(
			("potential memory corruption from utf8 string overflow. "
			 "expected={} bytes got={}"),
			dst.length(), len);
		std::abort();
	} else if (len < dst.length()) {
		spdlog::warn(
			("utf8 string not the specified length"
			 "expected={} bytes got={}"),
			dst.length(), len);
		dst.resize(len);
	}
}

static inline void
assign_cstring(std::string &dst, const TCollection_AsciiString &src) {
	dst.assign(src.ToCString(), src.Length());
}

static inline void
assign_cstring(std::string &dst, const TCollection_HAsciiString &src) {
	dst.assign(src.ToCString(), src.Length());
}

static bool
get_label_name(const TDF_Label &label, std::string &name)
{
	Handle(TDataStd_Name) attr;
	if (!label.FindAttribute(TDataStd_Name::GetID(), attr)) {
		return false;
	}

	assign_cstring(name, attr->Get());

	return true;
}

static bool
get_color_info(const TDF_Label &label, std::string &hexcode) {
	static const std::array types = {
		XCAFDoc_ColorGen,
		XCAFDoc_ColorSurf,
		XCAFDoc_ColorCurv,
	};

	Handle(TDataStd_TreeNode) node;
	Handle(XCAFDoc_Color) attr;

	for (const auto type : types) {
		if (label.FindAttribute(XCAFDoc::ColorRefGUID(type), node) &&
			node->HasFather() &&
			node->Father()->Label().FindAttribute(XCAFDoc_Color::GetID(), attr))
		{
			assign_cstring(hexcode, Quantity_ColorRGBA::ColorToHex(attr->GetColorRGBA()));

			return true;
		}
	}

	return false;
}

static bool
get_material_info(const TDF_Label &label, std::string &name, double &density)
{
	Handle(TDataStd_TreeNode) node;
	Handle(XCAFDoc_Material) attr;

	if (label.FindAttribute(XCAFDoc::MaterialRefGUID(), node) &&
		node->HasFather() &&
		node->Father()->Label().FindAttribute(XCAFDoc_Material::GetID(), attr))
	{
		assign_cstring(name, attr->GetName());
		density = attr->GetDensity();

		return true;
	}

	return false;
}

class collector {
	document doc;

	double minimum_volume;

	int label_num, n_small, n_negative_volume;

	void add_solids(const TDF_Label &label) {
		std::string color;
		std::string label_name{"unnammed"};
		std::string material_name{"unknown"};
		double material_density = 0;

		get_label_name(label, label_name);
		get_color_info(label, color);
		get_material_info(label, material_name, material_density);

		TopoDS_Shape shape;
		if (!XCAFDoc_ShapeTool::GetShape(label, shape)) {
			spdlog::error("unable to get shape {}", label_name);
			std::abort();
		}

		// add the solids to our list of things to do
		for (TopExp_Explorer ex{shape, TopAbs_SOLID}; ex.More(); ex.Next()) {
			const auto volume = volume_of_shape(shape);
			if (volume < minimum_volume) {
				if (volume < 0) {
					n_negative_volume += 1;
					spdlog::info(
						"ignoring part of shape '{}' due to negative volume, {}",
						label_name, volume);
				} else {
					n_small += 1;
					spdlog::info(
						"ignoring part of shape '{}' because it's too small, {} < {}",
						label_name, volume, minimum_volume);
				}
				continue;
			}

			doc.solid_shapes.emplace_back(ex.Current());

			fmt::print(
				"{},{},{:.1f},{},{},{}\n",
				label_num, label_name, volume, color, material_name, material_density);
		}
	}

public:
	collector(double minimum_volume) :
		minimum_volume{minimum_volume},
		label_num{0}, n_small{0}, n_negative_volume{0} {
	}

	void add_label(XCAFDoc_ShapeTool &shapetool, const TDF_Label &label) {
		TopoDS_Shape shape;

		label_num += 1;

		if (shapetool.IsAssembly(label)) {
			// loop over other labelled parts
			TDF_LabelSequence components;
			XCAFDoc_ShapeTool::GetComponents(label, components);
			for (auto const &comp : components) {
				add_label(shapetool, comp);
			}
		} else {
			add_solids(label);
		}
	}

	void log_summary() {
		spdlog::info(
			"enumerated {} labels, resulting in {} solids",
			label_num, doc.solid_shapes.size());
		if (n_small > 0) {
			spdlog::warn("{} solids were excluded because they were too small", n_small);
		}
		if (n_negative_volume > 0) {
			spdlog::warn("{} solids were excluded because they had negative volume", n_negative_volume);
		}
	}

	void fix_shapes(double precision, double max_tolerance) {
		for (auto &shape : doc.solid_shapes) {
			ShapeFix_Shape fixer{shape};
			fixer.SetPrecision(precision);
			fixer.SetMaxTolerance(max_tolerance);
			auto fixed = fixer.Perform();
			{
				std::string done;
				if (fixer.Status(ShapeExtend_DONE1)) done += ", some free edges were fixed";
				if (fixer.Status(ShapeExtend_DONE2)) done += ", some free wires were fixed";
				if (fixer.Status(ShapeExtend_DONE3)) done += ", some free faces were fixed";
				if (fixer.Status(ShapeExtend_DONE4)) done += ", some free shells were fixed";
				if (fixer.Status(ShapeExtend_DONE5)) done += ", some free solids were fixed";
				if (fixer.Status(ShapeExtend_DONE6)) done += ", shapes in compound(s) were fixed";
				spdlog::info("shapefixer={}{}",	fixed, done);
			}
			shape = fixer.Shape();
		}
	}

	void fix_wireframes(double precision, double max_tolerance) {
		for (auto &shape : doc.solid_shapes) {
			ShapeFix_Wireframe fixer{shape};
			fixer.SetPrecision(precision);
			fixer.SetMaxTolerance(max_tolerance);
			fixer.ModeDropSmallEdges() = Standard_True;
			{
				auto small_res = fixer.FixSmallEdges();
				std::string done;
				if (fixer.StatusSmallEdges(ShapeExtend_OK)) done += "No small edges were found";
				if (fixer.StatusSmallEdges(ShapeExtend_DONE1)) done += "Some small edges were fixed";
				if (fixer.StatusSmallEdges(ShapeExtend_FAIL1)) done += "Failed to fix some small edges";
				spdlog::info("smalledge={}, {}", small_res, done);
			}
			{
				auto gap_res = fixer.FixWireGaps();
				std::string done;
				if (fixer.StatusSmallEdges(ShapeExtend_OK)) done += "No gaps were found";
				if (fixer.StatusSmallEdges(ShapeExtend_DONE1)) done += "Some gaps in 3D were fixed";
				if (fixer.StatusSmallEdges(ShapeExtend_DONE2)) done += "Some gaps in 2D were fixed";
				if (fixer.StatusSmallEdges(ShapeExtend_FAIL1)) done += "Failed to fix some gaps in 3D";
				if (fixer.StatusSmallEdges(ShapeExtend_FAIL2)) done += "Failed to fix some gaps in 2D";
				spdlog::info(" wiregaps={}, {}", gap_res, done);
			}
			shape = fixer.Shape();
		}
	}

	bool check_geometry() {
		auto ninvalid = doc.count_invalid_shapes();
		if (ninvalid) {
			spdlog::critical("{} shapes were not valid", ninvalid);
			return false;
		}
		spdlog::info("geometry checks passed");
		return true;
	}

	void write_brep_file(const char *path) {
		doc.write_brep_file(path);
	}
};

static void
load_step_file(const char* path, collector &col) {
	auto app = XCAFApp_Application::GetApplication();

	STEPCAFControl_Reader reader;
	reader.SetNameMode(true);
	reader.SetColorMode(true);
	reader.SetMatMode(true);

	spdlog::info("reading step file {}", path);

	if (reader.ReadFile(path) != IFSelect_RetDone) {
		spdlog::critical("unable to ReadFile() on file");
		std::abort();
	}

	spdlog::debug("transferring into doc");

	Handle(TDocStd_Document) doc;
	app->NewDocument("MDTV-XCAF", doc);
	if (!reader.Transfer(doc)) {
		spdlog::critical("failed to Transfer into document");
		std::abort();
	}

	spdlog::debug("getting toplevel shapes");

	TDF_LabelSequence toplevel;
	auto shapetool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());

	shapetool->GetFreeShapes(toplevel);

	spdlog::debug("loading {} toplevel shape(s)", toplevel.Length());
	for (const auto &label : toplevel) {
		col.add_label(*shapetool, label);
	}
}

int
main(int argc, char **argv)
{
	configure_spdlog();

	std::string path_in, path_out;
	double minimum_volume = 1;
	bool check_geometry{true}, fix_geometry{false};

	{
		CLI::App app{"Convert STEP files to BREP format for preprocessor."};
		app.add_option("input", path_in, "Path of the input file")
			->required()
			->option_text("file.step");
		app.add_option("output", path_out, "Path of the output file")
			->required()
			->option_text("file.brep");
		app.add_option(
			"--min-volume,-v",
			minimum_volume,
			fmt::format("Minimum shape volume, in mm^3 ({:L})", minimum_volume));
		app.add_flag(
			"--check-geometry,!--no-check-geometry",
			check_geometry,
			fmt::format("Check overall validity of shapes ({})", check_geometry));
		app.add_flag(
			"--fix-geometry,!--no-fix-geometry",
			fix_geometry,
			fmt::format("Fix-up wireframes and shapes in geometry ({})", fix_geometry));
		CLI11_PARSE(app, argc, argv);
	}

	if (minimum_volume < 0) {
		spdlog::critical(
			"minimum shape volume ({}) should not be negative",
			minimum_volume);
		return 1;
	}

	collector doc(minimum_volume);
	load_step_file(path_in.c_str(), doc);

	if (check_geometry) {
		spdlog::debug("checking geometry");
		if (!doc.check_geometry()) {
			return 1;
		}
	}

	if (fix_geometry) {
		spdlog::debug("fixing wireframes");
		doc.fix_wireframes(0.01, 0.00001);
		spdlog::debug("fixing shapes");
		doc.fix_shapes(0.01, 0.00001);
	}

	doc.log_summary();

	doc.write_brep_file(path_out.c_str());

	spdlog::debug("done");

	return 0;
}
