#include <array>
#include <cstdlib>
#include <string>

#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

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

#include "document.hpp"

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
	TopoDS_Builder builder;
	TopoDS_CompSolid merged;

	int label_num, n_solid;

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
			builder.Add(merged, ex.Current());
			n_solid += 1;

			fmt::print(
				"{},{},{},{},{}\n",
				label_num, label_name, color, material_name, material_density);
		}
	}

public:
	collector() : label_num{0}, n_solid{0} {
		builder.MakeCompSolid(merged);
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
			label_num, n_solid);
	}

	void write_brep_file(const char *path) {
		spdlog::info("writing brep file {}", path);
		if (!BRepTools::Write(merged, path)) {
			spdlog::critical("failed to write brep file");
			std::abort();
		}
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
	// pull config from environment variables, e.g. `export SPDLOG_LEVEL=info,mylogger=trace`
	spdlog::cfg::load_env_levels();

	CLI::App app{"Convert STEP files to BREP format for preprocessor."};
	std::string path_in, path_out;
	app.add_option("input", path_in, "Path of the input file")
		->required()
		->option_text("file.step");
	app.add_option("output", path_out, "Path of the output file")
		->required()
		->option_text("file.brep");

	CLI11_PARSE(app, argc, argv);

	collector doc;
	load_step_file(path_in.c_str(), doc);

	doc.log_summary();

	doc.write_brep_file(path_out.c_str());

	spdlog::debug("done");

	return 0;
}
