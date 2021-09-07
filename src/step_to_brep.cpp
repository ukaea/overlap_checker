#include <array>
#include <cstdlib>
#include <string>

#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include <XCAFApp_Application.hxx>
#include <XCAFDoc.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_Material.hxx>
#include <XCAFDoc_Color.hxx>

#include <STEPCAFControl_Reader.hxx>

#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_TreeNode.hxx>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

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

	if (!label.FindAttribute(XCAFDoc::MaterialRefGUID(), node) ||
		!node->HasFather() ||
		!node->Father()->Label().FindAttribute(XCAFDoc_Material::GetID(), attr))
	{
		return false;
	}

	assign_cstring(name, attr->GetName());
	density = attr->GetDensity();

	return true;
}

class collector {
	TopoDS_Builder builder;
	TopoDS_CompSolid merged;

	int n_compound, n_solid, n_shell, n_other;

public:
	collector() : n_compound{0}, n_solid{0}, n_shell{0}, n_other{0} {
		builder.MakeCompSolid(merged);
	}

	void add_label(const TDF_Label &label, const int depth) {
		TopoDS_Shape shape;

		std::string label_name{"unnammed"};
		get_label_name(label, label_name);

		if (!XCAFDoc_ShapeTool::GetShape(label, shape)) {
			spdlog::error("unable to get shape {}", label_name);
			std::abort();
		}

		const auto stype = shape.ShapeType();
		switch(stype) {
		case TopAbs_COMPOUND: n_compound += 1; break;
		case TopAbs_SOLID: n_solid += 1; break;
		// PPP implicitly adds all shells to solids as well
		case TopAbs_SHELL: n_shell += 1; break;
		default: n_other += 1; break;
		}

		if (stype == TopAbs_SOLID) {
			builder.Add(merged, shape);

			std::string color;
			std::string material_name{"unknown"};
			double material_density = 0;

			get_color_info(label, color);
			get_material_info(label, material_name, material_density);

			fmt::print(
				"{},{},{},{},{}\n",
				n_solid, label_name, color, material_name, material_density);
		}

		TDF_LabelSequence components;
		XCAFDoc_ShapeTool::GetComponents(label, components);
		for (auto const &comp : components) {
			add_label(comp, depth+1);
		}
	}

	void log_summary() {
		spdlog::info(
			"total shapes found solid={}, shell={}, compounds={}, others={}",
			n_solid, n_shell, n_compound, n_other);
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
	reader.SetColorMode(true);
	reader.SetNameMode(true);
	reader.SetMatMode(true);

	spdlog::info("reading step file {}", path);

	if (reader.ReadFile(path) != IFSelect_RetDone) {
		spdlog::critical("unable to ReadFile() on file");
		std::abort();
	}

	spdlog::debug("transferring into doc");

	Handle(TDocStd_Document) doc;
	app->NewDocument(TCollection_ExtendedString("MDTV-CAF"), doc);
	if (!reader.Transfer(doc)) {
		spdlog::critical("failed to Transfer into document");
		std::abort();
	}

	spdlog::debug("getting toplevel shapes");

	TDF_LabelSequence toplevel;
	XCAFDoc_DocumentTool::ShapeTool(doc->Main())->GetFreeShapes(toplevel);

	spdlog::debug("loading {} toplevel shape(s)", toplevel.Length());
	for (const auto &label : toplevel) {
		col.add_label(label, 0);
	}
}

int
main()
{
	// pull config from environment variables, e.g. `export SPDLOG_LEVEL=info,mylogger=trace`
	spdlog::cfg::load_env_levels();

	const char *inp = "../../data/mastu.stp";

	collector doc;
	load_step_file(inp, doc);

	doc.log_summary();

	const char *out = "mastu.brep";
	doc.write_brep_file(out);

	spdlog::debug("done");

	return 0;
}
