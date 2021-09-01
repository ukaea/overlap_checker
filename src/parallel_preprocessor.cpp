#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/cfg/env.h>

#include "document.hpp"

// mess of opencascade headers
#include <TopoDS_Shape.hxx>
#include <TopoDS_Iterator.hxx>
#include <TDF_ChildIterator.hxx>

#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepAlgoAPI_BuilderAlgo.hxx>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <BRepBndLib.hxx>
#include <Bnd_OBB.hxx>
#include <Bnd_Box.hxx>

class my_formatter_flag : public spdlog::custom_flag_formatter
{
    using clock = std::chrono::steady_clock;
	using timepoint = std::chrono::time_point<clock>;

    timepoint reference;

public:
	my_formatter_flag() : reference{clock::now()} {}
	my_formatter_flag(timepoint ref) : reference{ref} {}

	void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override
    {
		auto elapsed = std::chrono::duration<double>(clock::now() - reference);
		auto txt = fmt::format("{:.3f}", elapsed.count());
        dest.append(txt.data(), txt.data() + txt.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<my_formatter_flag>(reference);
    }
};

template <> struct fmt::formatter<Bnd_OBB>: formatter<string_view> {
  // parse is inherited from formatter<string_view>.
	template <typename FormatContext>
	auto format(Bnd_OBB obb, FormatContext& ctx) {
		std::stringstream ss;
		ss << '{';
		obb.DumpJson(ss);
		ss << '}';
		return formatter<string_view>::format(ss.str(), ctx);
	}
};

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


static bool
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

static bool
are_bboxs_disjoint(const Bnd_OBB &b1, const Bnd_OBB& b2, double tolerance)
{
	if (tolerance > 0) {
		Bnd_OBB e1{b1}, e2{b2};
		e1.Enlarge(tolerance);
		e2.Enlarge(tolerance);
		return e1.IsOut(e2);
	}
	return b1.IsOut(b2);
}

static double
distance_between_shapes(const TopoDS_Shape& s1, const TopoDS_Shape& s2)
{
	auto dss = BRepExtrema_DistShapeShape(s1, s2, Extrema_ExtFlag_MIN);

	if (dss.Perform()) {
		return dss.Value();
	}

	spdlog::critical("BRepExtrema_DistShapeShape::Perform() failed");
	dss.Dump(std::cerr);

	std::abort();
}

int
main(int argc, char **argv)
{
	// pull config from environment variables, e.g. `export SPDLOG_LEVEL=info,mylogger=trace`
	spdlog::cfg::load_env_levels();

	auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<my_formatter_flag>('*').set_pattern("[%*] [%^%l%$] %v");
    spdlog::set_formatter(std::move(formatter));

	if(argc != 1) {
		fmt::print(stderr, "{} takes no arguments.\n", argv[0]);
		return 1;
	}

	const char *path = "mastu.brep";

	document doc;
	doc.load_brep_file(path);

	// AFAICT: from this point only doc.solid_shapes is used by original code,
	// searching for mySolids in the original code seems to have lots of
	// relevant hits

	/*
	** Geom::GeometryShapeChecker processor
	*/
	if (false) {
		spdlog::info("checking geometry");
		bool all_ok = true;
		for (const auto &shape : doc.solid_shapes) {
			if (!is_shape_valid(shape)) {
				all_ok = false;
			}
		}
		if (!all_ok) {
			spdlog::critical("some shapes were not valid");
			return 1;
		}
	}

	/*
	** Geom::GeometryPropertyBuilder processor
	*/
	if (false) {
		spdlog::info("caching geometry properties");
		// original code runs OccUtils::geometryProperty(shape) on every @shape.

		// why?  just so that _metadata.json could be written out?
		//
		//  * hashes for later referencing material properties seems useful

		// is this just because the code was using the shape Explorer rather
		// than Iterator, hence ordering could get weird
	}

	/*
	** Geom::BoundBoxBuilder processor
	*
	* just use Orientated Bounding Boxes for now, worry about compatibility
	* later!
	*/
	spdlog::info("calculating orientated bounding boxes");
	std::vector<Bnd_OBB> bounding_boxes;
	bounding_boxes.reserve(doc.solid_shapes.size());
	for (const auto &shape : doc.solid_shapes) {
		Bnd_OBB obb;
		BRepBndLib::AddOBB(shape, obb);
		bounding_boxes.push_back(obb);
	}

	/*
	** Geom::GeometryImprinter
	*/

	spdlog::info("starting imprinting");

	// need more descriptive names for tolerance and clearance
	const double
		imprint_tolerance = 0.001,
		imprint_clearance = 0.5,
		imprint_max_overlapping_volume_ratio = 0.1;

	if (imprint_tolerance <= 0) {
		spdlog::critical(
			"imprinting tolerance ({}) should be positive",
			imprint_tolerance);
		return 1;
	}

	if (imprint_clearance <= 0) {
		spdlog::critical(
			"imprinting clearance ({}) should be positive",
			imprint_clearance);
		return 1;
	}

	if (imprint_clearance <= imprint_tolerance) {
		spdlog::critical(
			"imprinting clearance ({}) should be larger than tolerance ({})",
			imprint_clearance, imprint_tolerance);
		return 1;
	}

	if (imprint_max_overlapping_volume_ratio <= 0 ||
		imprint_max_overlapping_volume_ratio >= 1) {
		spdlog::critical(
			"imprinting max overlapping volume ratio ({}) should be in (0., 1.)",
			imprint_max_overlapping_volume_ratio);
		return 1;
	}

	for (size_t hi = 1; hi < doc.solid_shapes.size() && hi < 100; hi++) {
		for (size_t lo = 0; lo < hi; lo++) {
			// seems reasonable to assume majority of shapes aren't close to
			// overlapping, so check with coarser limit first
			if (are_bboxs_disjoint(
					bounding_boxes[hi], bounding_boxes[lo], imprint_clearance)) {
				continue;
			}

			// original code makes copies of shapes as they are modified,
			// due a bug when using fuzzy fusing
			// http://dev.opencascade.org/index.php?q=node/1056#comment-520
			// this has been fixed since version 7.1 (~2017)
			TopTools_ListOfShape shapes;
			shapes.Append(doc.solid_shapes[hi]);
			shapes.Append(doc.solid_shapes[lo]);

			BRepAlgoAPI_BuilderAlgo fuser;
			fuser.SetNonDestructive(true);
			fuser.SetRunParallel(false);
			fuser.SetArguments(shapes);
			fuser.SetFuzzyValue(imprint_tolerance);

			fuser.Build();
			if (!fuser.IsDone()) {
				spdlog::critical("BRepAlgoAPI_BuilderAlgo::Build() failed");
				fuser.DumpErrors(std::cerr);
				std::abort();
			}

			if (fuser.HasDeleted()) {
				spdlog::critical(
					"fuser has done something interesting between shapes {} and {}",
					hi, lo);

				spdlog::critical(
					"fuser output  mod={} gen={} del={}",
					fuser.HasModified(), fuser.HasGenerated(), fuser.HasDeleted());

				// let the debugger get involved!
				// std::abort();
			}

			if (!fuser.HasModified() && !fuser.HasGenerated()) {
				continue;
			}

			if (fuser.HasGenerated()) {
				spdlog::debug("generated shapes for pair {} and {}", hi, lo);

				fuser.History()->Dump(std::cerr);
			}

			{
				// add in imprint_tolerance to help with numerical stability
				double
					original_volume = imprint_tolerance,
					fused_volume = imprint_tolerance;
				for (const auto& s : shapes) {
					GProp_GProps props;
					BRepGProp::VolumeProperties(s, props);
					original_volume += props.Mass();
				}

				{
					GProp_GProps props;
					BRepGProp::VolumeProperties(fuser.Shape(), props);
					fused_volume += props.Mass();
				}

				const double ratio_change = fused_volume / original_volume - 1;

				spdlog::info(
					"volumes of {} and {} change by {:.4} %",
					hi, lo, 100 * ratio_change);

				if (std::fabs(ratio_change) > imprint_max_overlapping_volume_ratio) {
					spdlog::error(
						"too much overlap between shapes {} and {}",
						lo, hi);
				}
			}
		}
	}

	return 0;
}
