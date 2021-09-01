#include <cstdlib>
#include <string>

#include <spdlog/spdlog.h>

#include "document.hpp"


#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <STEPCAFControl_Reader.hxx>

#include <TopAbs_ShapeEnum.hxx>
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

static void
add_xcaf_shape(document &doc,  const TDF_Label &label, const int depth=0)
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
		doc.compound_shapes.push_back(shape);
		break;
	case TopAbs_COMPSOLID:
	case TopAbs_SOLID:
		doc.solid_shapes.push_back(shape);
		break;
	case TopAbs_SHELL:
		// PPP implicitly adds all shells to solids as well
		doc.shell_shapes.push_back(shape);
		break;
	default:
		doc.other_shapes.push_back(shape);
		break;
	}

	TDF_LabelSequence components;
	XCAFDoc_ShapeTool::GetComponents(label, components);
	for (auto const &comp : components) {
		add_xcaf_shape(doc, comp, depth+1);
	}

	// maybe also do this, but seems redundant given the above iterator
	/*
	for (TDF_ChildIterator it{label}; it.More(); it.Next()) {
		add_xcaf_shape(shapetool, it.Value());
	}
	*/
}

static void
summary(const document &doc) {
	spdlog::info(
		"total shapes found solid={}, shell={}, compounds={}, others={}",
		doc.solid_shapes.size(), doc.shell_shapes.size(),
		doc.compound_shapes.size(), doc.other_shapes.size());
}

void
document::load_brep_file(const char* path)
{
	BRep_Builder builder;
	TopoDS_Shape shape;

	spdlog::info("reading brep file {}", path);

	if (!BRepTools::Read(shape, path, builder)) {
		spdlog::critical("unable to read BREP file");
		std::abort();
	}

	if (shape.ShapeType() != TopAbs_COMPSOLID) {
		spdlog::critical("expected to get COMPSOLID toplevel shape from brep file");
		std::abort();
	}

	spdlog::debug("expecting {} solid shapes", shape.NbChildren());
	solid_shapes.reserve(shape.NbChildren());

	for (TopoDS_Iterator it(shape); it.More(); it.Next()) {
		const auto &shp = it.Value();
		if (shp.ShapeType() != TopAbs_SOLID) {
			spdlog::critical("expecting shape to be a solid");
			std::abort();
		}

		solid_shapes.push_back(shp);
	}

	summary(*this);
}

void
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
		std::abort();
	}

	spdlog::debug("transferring into doc");

	Handle(TDocStd_Document) tdoc;
	app->NewDocument(TCollection_ExtendedString("MDTV-CAF"), tdoc);
	if (!reader.Transfer(tdoc)) {
		spdlog::critical("failed to Transfer into document");
		std::abort();
	}

	spdlog::debug("getting toplevel shapes");

	TDF_LabelSequence toplevel;
	XCAFDoc_DocumentTool::ShapeTool(tdoc->Main())->GetFreeShapes(toplevel);

	spdlog::debug("loading {} toplevel shape(s)", toplevel.Length());
	for (const auto &label : toplevel) {
		add_xcaf_shape(*this, label);
	}

	summary(*this);
}

void
document::write_brep_file(const char* path)
{
	spdlog::debug("building brep compsolid");

	TopoDS_Builder builder;
	TopoDS_CompSolid merged;
	builder.MakeCompSolid(merged);

	for (const auto& item : solid_shapes) {
		builder.Add(merged, item);
	}

	spdlog::info("writing brep file {}", path);
	if (!BRepTools::Write(merged, path)) {
		spdlog::critical("failed to write brep file");
		std::abort();
	}
}
