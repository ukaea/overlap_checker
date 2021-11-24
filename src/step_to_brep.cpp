#include <array>
#include <cstdlib>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>

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

#include <cxx_argp_parser.h>
#include <aixlog.hpp>

#include "document.hpp"
#include "utils.hpp"

static void
assign_cstring(std::string &dst, const TCollection_ExtendedString &src) {
	dst.resize(src.LengthOfCString());
	auto str = (char *)dst.data();
	size_t len = src.ToUTF8CString(str);

	if (len > dst.length()) {
		LOG(FATAL)
			<< "potential memory corruption from utf8 string overflow. "
			<< "expected=" << dst.length() << "bytes "
			<< "got=" << len << '\n';
		std::abort();
	} else if (len < dst.length()) {
		LOG(WARNING)
			<< "utf8 string not the specified length"
			<< "expected=" << dst.length() << " bytes "
			<< "got=" << len << '\n';
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
	static const std::array<XCAFDoc_ColorType, 3> types = {
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
			LOG(ERROR) << "unable to get shape " << label_name << '\n';
			std::abort();
		}

		// add the solids to our list of things to do
		for (TopExp_Explorer ex{shape, TopAbs_SOLID}; ex.More(); ex.Next()) {
			const auto volume = volume_of_shape(shape);
			if (volume < minimum_volume) {
				if (volume < 0) {
					n_negative_volume += 1;
					LOG(INFO)
						<< "ignoring part of shape '" << label_name << "' "
						<< "due to negative volume, " << volume << '\n';
				} else {
					n_small += 1;
					LOG(INFO)
						<< "ignoring part of shape '" << label_name << "' "
						<< "because it's too small, " << volume
						<< " < " << minimum_volume << '\n';
				}
				continue;
			}

			doc.solid_shapes.emplace_back(ex.Current());

			const auto ss = std::cout.precision();
			std::cout
				<< label_num << ','
				<< label_name << ','
				<< std::setprecision(1) << volume << std::setprecision(ss) << ','
				<< color << ','
				<< material_name << ','
				<< material_density << '\n';
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
		LOG(INFO)
			<< "enumerated " << label_num << " labels, "
			<< "resulting in " << doc.solid_shapes.size() << " solids\n";
		if (n_small > 0) {
			LOG(WARNING)
				<< n_small << " solids were excluded because they were too small\n";
		}
		if (n_negative_volume > 0) {
			LOG(WARNING)
				<< n_negative_volume << " solids were excluded because they had negative volume\n";
		}
	}

	void fix_shapes(double precision, double max_tolerance) {
		for (auto &shape : doc.solid_shapes) {
			ShapeFix_Shape fixer{shape};
			fixer.SetPrecision(precision);
			fixer.SetMaxTolerance(max_tolerance);
			auto fixed = fixer.Perform();
			LOG(INFO) << "shapefixer=" << fixed;
			if (fixer.Status(ShapeExtend_DONE1)) LOG(INFO) << ", some free edges were fixed";
			if (fixer.Status(ShapeExtend_DONE2)) LOG(INFO) << ", some free wires were fixed";
			if (fixer.Status(ShapeExtend_DONE3)) LOG(INFO) << ", some free faces were fixed";
			if (fixer.Status(ShapeExtend_DONE4)) LOG(INFO) << ", some free shells were fixed";
			if (fixer.Status(ShapeExtend_DONE5)) LOG(INFO) << ", some free solids were fixed";
			if (fixer.Status(ShapeExtend_DONE6)) LOG(INFO) << ", shapes in compound(s) were fixed";
			LOG(INFO) << '\n';
			shape = fixer.Shape();
		}
	}

	void fix_wireframes(double precision, double max_tolerance) {
		for (auto &shape : doc.solid_shapes) {
			ShapeFix_Wireframe fixer{shape};
			fixer.SetPrecision(precision);
			fixer.SetMaxTolerance(max_tolerance);
			fixer.ModeDropSmallEdges() = Standard_True;
			auto small_res = fixer.FixSmallEdges();
			auto gap_res = fixer.FixWireGaps();
			shape = fixer.Shape();

			LOG(INFO) << "smalledge=" << small_res;
			if (fixer.StatusSmallEdges(ShapeExtend_OK)) LOG(INFO) << "No small edges were found";
			if (fixer.StatusSmallEdges(ShapeExtend_DONE1)) LOG(INFO) << "Some small edges were fixed";
			if (fixer.StatusSmallEdges(ShapeExtend_FAIL1)) LOG(INFO) << "Failed to fix some small edges";
			LOG(INFO) << '\n';

			LOG(INFO) << " wiregaps=" << gap_res;
			if (fixer.StatusWireGaps(ShapeExtend_OK)) LOG(INFO) << "No gaps were found";
			if (fixer.StatusWireGaps(ShapeExtend_DONE1)) LOG(INFO) << "Some gaps in 3D were fixed";
			if (fixer.StatusWireGaps(ShapeExtend_DONE2)) LOG(INFO) << "Some gaps in 2D were fixed";
			if (fixer.StatusWireGaps(ShapeExtend_FAIL1)) LOG(INFO) << "Failed to fix some gaps in 3D";
			if (fixer.StatusWireGaps(ShapeExtend_FAIL2)) LOG(INFO) << "Failed to fix some gaps in 2D";
			LOG(INFO) << '\n';
		}
	}

	bool check_geometry() {
		auto ninvalid = doc.count_invalid_shapes();
		if (ninvalid) {
			LOG(FATAL) << ninvalid << " shapes were not valid\n";
			return false;
		}
		LOG(INFO) << "geometry checks passed\n";
		return true;
	}

	void write_brep_file(const char *path) {
		doc.write_brep_file(path);
	}
};

static bool
load_step_file(const char* path, collector &col) {
	auto app = XCAFApp_Application::GetApplication();

	STEPCAFControl_Reader reader;
	reader.SetNameMode(true);
	reader.SetColorMode(true);
	reader.SetMatMode(true);

	LOG(INFO) << "reading step file " << path << '\n';

	if (reader.ReadFile(path) != IFSelect_RetDone) {
		LOG(FATAL) << "unable to read STEP file " << path << '\n';
		return false;
	}

	LOG(DEBUG) << "transferring into doc";

	Handle(TDocStd_Document) doc;
	app->NewDocument("MDTV-XCAF", doc);
	if (!reader.Transfer(doc)) {
		LOG(FATAL) << "failed to Transfer into document";
		std::abort();
	}

	LOG(DEBUG) << "getting toplevel shapes\n";

	TDF_LabelSequence toplevel;
	auto shapetool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());

	shapetool->GetFreeShapes(toplevel);

	LOG(DEBUG)
		<< "loading " << toplevel.Length() << " toplevel shape(s)\n";
	for (const auto &label : toplevel) {
		col.add_label(*shapetool, label);
	}

	return true;
}

int
main(int argc, char **argv)
{
	configure_aixlog();

	std::string path_in, path_out;
	double minimum_volume = 1;
	bool check_geometry{true}, fix_geometry{false};

	{
		const char *doc = "Convert STEP files to BREP format for preprocessor.";
		const char *usage = "input.step output.brep";

		std::stringstream stream;

		stream << "Minimum shape volume (" << minimum_volume << " mm^3)";
		auto min_volume_help = stream.str();

		stream = {};
		stream << "Check overall validity of shapes (" << (check_geometry ? "yes" : "no") << ")";
		auto check_geometry_help = stream.str();

		stream = {};
		stream << "Fix-up wireframes and shapes in geometry (" << (fix_geometry ? "yes" : "no") << ")";
		auto fix_geometry_help = stream.str();

		tool_argp_parser argp(2);
		argp.add_option(
			{"min-volume", 1023, "volume", 0, min_volume_help.c_str()},
			minimum_volume);
		argp.add_option(
			{"check-geometry", 1024, nullptr, 0, check_geometry_help.c_str()},
			check_geometry);
		argp.add_option(
			{"no-check-geometry", 1025, nullptr, OPTION_ALIAS},
			[&check_geometry](const char *) { check_geometry = false; return true; });
		argp.add_option(
			{"fix-geometry", 1026, nullptr, 0, fix_geometry_help.c_str()},
			fix_geometry);
		argp.add_option(
			{"no-fix-geometry", 1027, nullptr, OPTION_ALIAS},
			[&fix_geometry](const char *) { fix_geometry = false; return true; });

		if (!argp.parse(argc, argv, usage, doc)) {
			return 1;
		}

		const auto &args = argp.arguments();
		assert(args.size() == 2);
		path_in = args[0];
		path_out = args[1];
	}

	if (minimum_volume < 0) {
		LOG(FATAL)
			<< "minimum shape volume "
			<< '(' << minimum_volume << ") should not be negative\n";
		return 1;
	}

	collector doc(minimum_volume);
	if (!load_step_file(path_in.c_str(), doc)) {
		return 1;
	}

	if (check_geometry) {
		LOG(DEBUG) << "checking geometry\n";
		if (!doc.check_geometry()) {
			return 1;
		}
	}

	if (fix_geometry) {
		LOG(DEBUG) << "fixing wireframes\n";
		doc.fix_wireframes(0.01, 0.00001);
		LOG(DEBUG) << "fixing shapes\n";
		doc.fix_shapes(0.01, 0.00001);
	}

	doc.log_summary();

	doc.write_brep_file(path_out.c_str());

	LOG(DEBUG) << "done\n";

	return 0;
}
