#include <cstdlib>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

// mess of opencascade headers
#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>

#include <TopExp_Explorer.hxx>
#include <TDF_ChildIterator.hxx>

#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <STEPCAFControl_Reader.hxx>


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

template <> struct fmt::formatter<BRepCheck_Status>: formatter<string_view> {
  // parse is inherited from formatter<string_view>.
  template <typename FormatContext>
  auto format(BRepCheck_Status c, FormatContext& ctx) {
    string_view name = "unknown";
	switch(c) {
	case BRepCheck_NoError: name = "NoError"; break;
	case BRepCheck_InvalidPointOnCurve: name = "InvalidPointOnCurve"; break;
	case BRepCheck_InvalidPointOnCurveOnSurface: name = "InvalidPointOnCurveOnSurface"; break;
	case BRepCheck_InvalidPointOnSurface: name = "InvalidPointOnSurface"; break;
	case BRepCheck_No3DCurve: name = "No3DCurve"; break;
	case BRepCheck_Multiple3DCurve: name = "Multiple3DCurve"; break;
	case BRepCheck_Invalid3DCurve: name = "Invalid3DCurve"; break;
	case BRepCheck_NoCurveOnSurface: name = "NoCurveOnSurface"; break;
	case BRepCheck_InvalidCurveOnSurface: name = "InvalidCurveOnSurface"; break;
	case BRepCheck_InvalidCurveOnClosedSurface: name = "InvalidCurveOnClosedSurface"; break;
	case BRepCheck_InvalidSameRangeFlag: name = "InvalidSameRangeFlag"; break;
	case BRepCheck_InvalidSameParameterFlag: name = "InvalidSameParameterFlag"; break;
	case BRepCheck_InvalidDegeneratedFlag: name = "InvalidDegeneratedFlag"; break;
	case BRepCheck_FreeEdge: name = "FreeEdge"; break;
	case BRepCheck_InvalidMultiConnexity: name = "InvalidMultiConnexity"; break;
	case BRepCheck_InvalidRange: name = "InvalidRange"; break;
	case BRepCheck_EmptyWire: name = "EmptyWire"; break;
	case BRepCheck_RedundantEdge: name = "RedundantEdge"; break;
	case BRepCheck_SelfIntersectingWire: name = "SelfIntersectingWire"; break;
	case BRepCheck_NoSurface: name = "NoSurface"; break;
	case BRepCheck_InvalidWire: name = "InvalidWire"; break;
	case BRepCheck_RedundantWire: name = "RedundantWire"; break;
	case BRepCheck_IntersectingWires: name = "IntersectingWires"; break;
	case BRepCheck_InvalidImbricationOfWires: name = "InvalidImbricationOfWires"; break;
	case BRepCheck_EmptyShell: name = "EmptyShell"; break;
	case BRepCheck_RedundantFace: name = "RedundantFace"; break;
	case BRepCheck_InvalidImbricationOfShells: name = "InvalidImbricationOfShells"; break;
	case BRepCheck_UnorientableShape: name = "UnorientableShape"; break;
	case BRepCheck_NotClosed: name = "NotClosed"; break;
	case BRepCheck_NotConnected: name = "NotConnected"; break;
	case BRepCheck_SubshapeNotInShape: name = "SubshapeNotInShape"; break;
	case BRepCheck_BadOrientation: name = "BadOrientation"; break;
	case BRepCheck_BadOrientationOfSubshape: name = "BadOrientationOfSubshape"; break;
	case BRepCheck_InvalidPolygonOnTriangulation: name = "InvalidPolygonOnTriangulation"; break;
	case BRepCheck_InvalidToleranceValue: name = "InvalidToleranceValue"; break;
	case BRepCheck_EnclosedRegion: name = "EnclosedRegion"; break;
	case BRepCheck_CheckFail: name = "CheckFail"; break;
    }
    return formatter<string_view>::format(name, ctx);
  }
};


std::string
get_label_name(const TDF_Label &label)
{
	Handle(TDataStd_Name) name;

	if (!label.FindAttribute(name->GetID(), name)) {
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


struct document {
	std::vector<TopoDS_Shape> solid_shapes;
	std::vector<TopoDS_Shape> shell_shapes;
	std::vector<TopoDS_Shape> compound_shapes;
	std::vector<TopoDS_Shape> other_shapes;

	void summary();

	void add_xcaf_shape(XCAFDoc_ShapeTool &shapetool, const TDF_Label &label, const int depth=0);
};

void document::summary() {
	spdlog::info(
		"total shapes found solid={}, shell={}, compounds={}, others={}",
		solid_shapes.size(), shell_shapes.size(), compound_shapes.size(), other_shapes.size());
}

void
document::add_xcaf_shape(XCAFDoc_ShapeTool &shapetool, const TDF_Label &label, const int depth)
{
	TopoDS_Shape shape;
	if (!shapetool.GetShape(label, shape)) {
		spdlog::error("unable to get shape {}", get_label_name(label));
		return;
	}

	if (depth == 0) {
		spdlog::debug("got {} name='{}'", shape.ShapeType(), get_label_name(label));
	} else {
		spdlog::debug("{:*>{}} {} name='{}'", "", depth*2, shape.ShapeType(), get_label_name(label));
	}

	// fmt::print("{}\n", get_label_name(label));

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
	shapetool.GetComponents(label, components);
	for (auto const &comp : components) {
		add_xcaf_shape(shapetool, comp, depth+1);
	}

	// maybe also do this, but seems redundant with the above iterator
	/*
	for (TDF_ChildIterator it{label}; it.More(); it.Next()) {
		add_xcaf_shape(shapetool, it.Value());
	}
	*/
}

bool
is_shape_valid(const TopoDS_Shape& shape)
{
	BRepCheck_Analyzer checker(shape);

	if (checker.IsValid()) {
		return true;
	}
	std::vector<BRepCheck_Status> errors;

	const auto &result = checker.Result(shape);

	for (const auto &status : result->StatusOnShape()) {
		if (status != BRepCheck_NoError) {
			errors.push_back(status);
		}
	}

	for (TopoDS_Iterator it{shape}; it.More(); it.Next()) {
		const auto &component = it.Value();

		if (checker.IsValid(component)) {
			continue;
		}

		for (const auto &status : result->StatusOnShape(component)) {
			if (status != BRepCheck_NoError) {
				errors.push_back(status);
			}
		}
	}

	spdlog::warn(
		"shape contains following errors {}",
		errors);

	return false;
}

int main(int argc, char **argv) {
	// pull config from environment variables, e.g. `export SPDLOG_LEVEL=info,mylogger=trace`
    spdlog::cfg::load_env_levels();

	if(argc != 1) {
		fmt::print(stderr, "{} takes no arguments.\n", argv[0]);
        return 1;
    }

	const double toleranceThreshold = 0.1;

	// const char *path = "../../data/mastu.stp";
	const char *path = "../data/test_geometry.step";

	auto app = XCAFApp_Application::GetApplication();

	STEPCAFControl_Reader reader;
	reader.SetColorMode(true);
	reader.SetNameMode(true);
	reader.SetMatMode(true);

	spdlog::info("reading the input file");

	if (reader.ReadFile(path) != IFSelect_RetDone) {
		spdlog::critical("unable to ReadFile() on file");
		return 1;
	}

	spdlog::debug("transferring into doc");

	Handle(TDocStd_Document) tdoc;
	app->NewDocument(TCollection_ExtendedString("MDTV-CAF"), tdoc);
	if (!reader.Transfer(tdoc)) {
		spdlog::critical("failed to Transfer into document");
		return 1;
	}

	spdlog::debug("getting toplevel shapes");

	Handle(XCAFDoc_ShapeTool) shapetool = XCAFDoc_DocumentTool::ShapeTool(tdoc->Main());

	TDF_LabelSequence toplevel;
	shapetool->GetFreeShapes(toplevel);

	document doc;

	spdlog::info("loading {} toplevel shape(s)", toplevel.Length());
	for (const auto &label : toplevel) {
		doc.add_xcaf_shape(*shapetool, label);
	}

	doc.summary();

	// AFAICT: from this point only doc.solid_shapes is used by original code,
	// searching for mySolids in the original code seems to have lots of
	// relevant hits

    /*
	** Geom::GeometryShapeChecker processor
	*/
	spdlog::debug("checking geometry");
	bool all_ok = true;
	for (const auto &shape : doc.solid_shapes) {
		if (!is_shape_valid(shape)) {
			all_ok = false;
		}
	}
	if (!all_ok) {
		spdlog::critical("some shapes where not valid");
		return 1;
	}

	/*
	** Geom::GeometryPropertyBuilder processor
	*/
	spdlog::debug("caching geometry properties");
	// original code runs OccUtils::geometryProperty(shape) on every @shape.

	// why?  just so that _metadata.json could be written out?
    //
	//  * hashes for later referencing material properties seems useful

	/*
	** Geom::BoundBoxBuilder processor
	*/
	spdlog::debug("calculating bounding boxes");
	std::vector<Bnd_Box> bounding_boxes;
	bounding_boxes.reserve(doc.solid_shapes.size());
	for (const auto &shape : doc.solid_shapes) {
		Bnd_Box bbox;
		bbox.SetGap(toleranceThreshold);
		BRepBndLib::Add(shape, bbox);
		bounding_boxes.push_back(bbox);

		gp_Pnt mn = bbox.CornerMin(), mx = bbox.CornerMax();
		spdlog::debug(
			"bbox min=({},{},{}) max=({},{},{})",
			mn.X(), mn.Y(), mn.Z(),
			mx.X(), mx.Y(), mx.Z());
	}

	for (size_t hi = 1; hi < doc.solid_shapes.size(); hi++) {
		const auto& bbox_hi = bounding_boxes[hi];
		for (size_t lo = 0; lo < hi; lo++) {
			const auto& bbox_lo = bounding_boxes[lo];
			if (bbox_hi.IsOut(bbox_lo) && bbox_lo.IsOut(bbox_hi)) {
				continue;
			}
			fmt::print("{} <> {}\n", hi, lo);

			// 
		}
	}

	return 0;
}
