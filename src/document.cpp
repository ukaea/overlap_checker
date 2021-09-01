#include <string>

#include <spdlog/spdlog.h>

#include "document.hpp"


#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <STEPCAFControl_Reader.hxx>

#include <TopExp_Explorer.hxx>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>

#include <TopoDS_Builder.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_CompSolid.hxx>


static std::string
get_label_name(const TDF_Label &label)
{
	Handle(TDataStd_Name) name;

	if (!label.FindAttribute(TDataStd_Name::GetID(), name)) {
		return {};
	}

	const auto &extstr = name->Get();

	std::string result(extstr.LengthOfCString(), 0);
	char * str = result.data();
	size_t len = extstr.ToUTF8CString(str);

	if (len > result.length()) {
		spdlog::critical(
			("potential memory corruption from utf8 string overflow. "
			 "expected={} bytes got={}"),
			result.length(), len);
		std::abort();
	} else if (len < result.length()) {
		result.resize(len);
	}

	return result;
}

bool
document::load_brep_file(const char* path)
{
	BRep_Builder builder;
	TopoDS_Shape shape;

	spdlog::info("reading brep file {}", path);

	if (!BRepTools::Read(shape, path, builder)) {
		spdlog::critical("unable to read BREP file");
		return false;
	}

	if (shape.ShapeType() != TopAbs_COMPSOLID) {
		spdlog::critical("expected to get COMPSOLID toplevel shape from brep file");
		return false;
	}

	spdlog::info("expecting {} toplevel shapes", shape.NbChildren());
	solid_shapes.reserve(shape.NbChildren());

	for (TopoDS_Iterator it(shape); it.More(); it.Next()) {
		solid_shapes.push_back(it.Value());
	}

	return true;
}

bool
document::load_step_file(const char* path)
{
	auto app = XCAFApp_Application::GetApplication();

	STEPCAFControl_Reader reader;
	reader.SetColorMode(true);
	reader.SetNameMode(true);
	reader.SetMatMode(true);

	spdlog::info("reading step file {}", path);

	if (reader.ReadFile(path) != IFSelect_RetDone) {
		spdlog::critical("unable to ReadFile() on file");
		return false;
	}

	spdlog::debug("transferring into doc");

	Handle(TDocStd_Document) tdoc;
	app->NewDocument(TCollection_ExtendedString("MDTV-CAF"), tdoc);
	if (!reader.Transfer(tdoc)) {
		spdlog::critical("failed to Transfer into document");
		return false;
	}

	spdlog::debug("getting toplevel shapes");

	TDF_LabelSequence toplevel;
	XCAFDoc_DocumentTool::ShapeTool(tdoc->Main())->GetFreeShapes(toplevel);

	spdlog::info("loading {} toplevel shape(s)", toplevel.Length());
	for (const auto &label : toplevel) {
		add_xcaf_shape(label);
	}

	return true;
}

void document::summary() {
	spdlog::info(
		"total shapes found solid={}, shell={}, compounds={}, others={}",
		solid_shapes.size(), shell_shapes.size(), compound_shapes.size(), other_shapes.size());
}


void
document::add_xcaf_shape(const TDF_Label &label, const int depth)
{
	TopoDS_Shape shape;
	if (!XCAFDoc_ShapeTool::GetShape(label, shape)) {
		spdlog::error("unable to get shape {}", get_label_name(label));
		return;
	}

	if (depth == 0) {
		spdlog::debug("got {} name='{}'", shape.ShapeType(), get_label_name(label));
	} else {
		spdlog::debug("{:*>{}} {} name='{}'", "", depth*2, shape.ShapeType(), get_label_name(label));
	}

	switch(shape.ShapeType()) {
	case TopAbs_COMPOUND:
		compound_shapes.push_back(shape);
		break;
	case TopAbs_COMPSOLID:
	case TopAbs_SOLID:
		solid_shapes.push_back(shape);
		break;
	case TopAbs_SHELL:
		// PPP implicitly adds all shells to solids as well
		shell_shapes.push_back(shape);
		break;
	default:
		other_shapes.push_back(shape);
		break;
	}

	TDF_LabelSequence components;
	XCAFDoc_ShapeTool::GetComponents(label, components);
	for (auto const &comp : components) {
		add_xcaf_shape(comp, depth+1);
	}

	// maybe also do this, but seems redundant given the above iterator
	/*
	for (TDF_ChildIterator it{label}; it.More(); it.Next()) {
		add_xcaf_shape(shapetool, it.Value());
	}
	*/
}

bool
document::write_brep_file(const char* path)
{
	spdlog::debug("building brep compsolid");

	TopoDS_Builder builder;
	TopoDS_CompSolid merged;
	builder.MakeCompSolid(merged);

	for (const auto& item : solid_shapes) {
		builder.Add(merged, item);
	}

	spdlog::info("writing brep file");
	return BRepTools::Write(merged, path);
}
