#ifdef INCLUDE_DOCTESTS
#include <doctest/doctest.h>
#endif

#include <fmt/ranges.h>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <spdlog/stopwatch.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <BOPAlgo_PaveFiller.hxx>
#include <BOPAlgo_Operation.hxx>

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>

#include <BRepCheck_Analyzer.hxx>

#include <BRepExtrema_DistShapeShape.hxx>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <TopoDS_Builder.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_CompSolid.hxx>

#include <Message_Report.hxx>
#include <Message_Gravity.hxx>

#include "document.hpp"


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

// code to allow spdlog to print out elapsed time since process started
class time_elapsed_formatter_flag : public spdlog::custom_flag_formatter
{
    using clock = std::chrono::steady_clock;
	using timepoint = std::chrono::time_point<clock>;

    timepoint reference;

public:
	time_elapsed_formatter_flag() : reference{clock::now()} {}
	time_elapsed_formatter_flag(timepoint ref) : reference{ref} {}

	void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override {
		auto elapsed = std::chrono::duration<double>(clock::now() - reference);
		auto txt = fmt::format("{:.3f}", elapsed.count());
        dest.append(txt.data(), txt.data() + txt.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<time_elapsed_formatter_flag>(reference);
    }
};

void configure_spdlog()
{
	// pull config from environment variables, e.g. `export SPDLOG_LEVEL=info,mylogger=trace`
	spdlog::cfg::load_env_levels();

	auto formatter = std::make_unique<spdlog::pattern_formatter>();
	formatter->add_flag<time_elapsed_formatter_flag>('*');
	formatter->set_pattern("[%*] [%^%l%$] %v");
    spdlog::set_formatter(std::move(formatter));

    // Replace the default logger with a (color, single-threaded) stderr
    // logger with name "" (but first replace it with an arbitrarily-named
    // logger to prevent a name clash)
    spdlog::set_default_logger(spdlog::stderr_color_mt("some_arbitrary_name"));
    spdlog::set_default_logger(spdlog::stderr_color_mt(""));
}

/** are floats close, i.e. approximately equal? due to floating point
 * representation we have to care about a couple of types of error, relative
 * and absolute.
 */
bool
are_vals_close(const double a, const double b, const double drel, const double dabs)
{
	assert(drel >= 0);
	assert(dabs >= 0);
	assert(drel > 0 || dabs > 0);

	const auto mag = std::max(std::abs(a), std::abs(b));

	return std::abs(b - a) < (drel * mag + dabs);
}

double
volume_of_shape(const TopoDS_Shape& shape)
{
	GProp_GProps props;
	BRepGProp::VolumeProperties(shape, props);
	return props.Mass();
}

double
distance_between_shapes(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
	auto dss = BRepExtrema_DistShapeShape(a, b, Extrema_ExtFlag_MIN);

	if (dss.Perform()) {
		return dss.Value();
	}

	spdlog::critical("BRepExtrema_DistShapeShape::Perform() failed");
	dss.Dump(std::cerr);

	std::abort();
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
}

static bool
is_shape_valid(const TopoDS_Shape& shape)
{
	BRepCheck_Analyzer checker{shape};
	if (checker.IsValid()) {
		return true;
	}

	const auto &result = checker.Result(shape);

	std::vector<BRepCheck_Status> errors;
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

size_t
document::count_invalid_shapes() {
	size_t num_invalid = 0;
	for (const auto &shape : solid_shapes) {
		if (!is_shape_valid(shape)) {
			num_invalid += 1;
		}
	}
	return num_invalid;
}

intersect_result classify_solid_intersection(
	const TopoDS_Shape& shape, const TopoDS_Shape& tool, double fuzzy_value)
{
	intersect_result result = {
		intersect_status::failed,
		// fuzzy value
		0.0,

		// number of warnings
		0, 0, 0,

		// volumes
		-1.0, -1.0, -1.0,
	};

	// explicitly construct a PaveFiller so we can reuse the work between
	// operations, at a minimum we want to perform sectioning and getting any
	// common solid
	BOPAlgo_PaveFiller filler;

	{
		TopTools_ListOfShape args;
		args.Append(shape);
		args.Append(tool);
		filler.SetArguments(args);
	}

	filler.SetRunParallel(false);
	filler.SetFuzzyValue(fuzzy_value);
	filler.SetNonDestructive(true);

	// this can be a very expensive call, e.g. 10+ seconds
	filler.Perform();

	{
		Handle(Message_Report) report = filler.GetReport();

		result.num_filler_warnings = report->GetAlerts(Message_Warning).Size();

		// this report is merged into the following reports
		report->Clear();
	}

	result.fuzzy_value = filler.FuzzyValue();

	if (filler.HasErrors()) {
		return result;
	}

	// I'm only using the Section class because it has the most convinient
	// constructor, the functionality mostly comes from
	// BRepAlgoAPI_BooleanOperation and BRepAlgoAPI_BuilderAlgo (at the time
	// of writing anyway!)
	BRepAlgoAPI_Section op{shape, tool, filler, false};
	op.SetOperation(BOPAlgo_COMMON);
	op.SetRunParallel(false);
	op.SetFuzzyValue(filler.FuzzyValue());
	op.SetNonDestructive(true);

	op.Build();

	result.num_common_warnings = op.GetReport()->GetAlerts(Message_Warning).Size();

	if (op.HasErrors()) {
		return result;
	}

	TopExp_Explorer ex;
	ex.Init(op.Shape(), TopAbs_SOLID);
	if (ex.More()) {
		result.vol_common = volume_of_shape(op.Shape());

		op.SetOperation(BOPAlgo_CUT);
		op.Build();
		if (op.HasErrors()) {
			return result;
		}
		result.vol_cut = volume_of_shape(op.Shape());

		op.SetOperation(BOPAlgo_CUT21);
		op.Build();
		if (op.HasErrors()) {
			return result;
		}
		result.vol_cut12 = volume_of_shape(op.Shape());

		result.status = intersect_status::overlap;
		return result;
	}

	op.SetOperation(BOPAlgo_SECTION);
	op.Build();

	result.num_section_warnings = op.GetReport()->GetAlerts(Message_Warning).Size();
	if (!op.HasErrors()) {
		ex.Init(op.Shape(), TopAbs_VERTEX);
		result.status = ex.More() ? intersect_status::touching : intersect_status::distinct;
	}

	return result;
}

#ifdef DOCTEST_LIBRARY_INCLUDED
#include <BRepPrimAPI_MakeBox.hxx>

static inline TopoDS_Shape
cube_at(double x, double y, double z, double length)
{
	return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), length, length, length).Shape();
}

TEST_SUITE("testing classify_solid_intersection") {
	TEST_CASE("two identical objects completely overlap") {
		const auto s1 = cube_at(0, 0, 0, 10), s2 = cube_at(0, 0, 0, 10);

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE_EQ(result.status, intersect_status::overlap);
		CHECK_EQ(result.vol_common, doctest::Approx(10*10*10));
		CHECK_EQ(result.vol_cut, doctest::Approx(0));
		CHECK_EQ(result.vol_cut12, doctest::Approx(0));
	}

	TEST_CASE("smaller object contained in larger one overlap") {
		const auto s1 = cube_at(0, 0, 0, 10), s2 = cube_at(2, 2, 2, 6);

		const double
			v1 = 10*10*10,
			v2 = 6*6*6;

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE_EQ(result.status, intersect_status::overlap);
		CHECK_EQ(result.vol_common, doctest::Approx(v2));
		CHECK_EQ(result.vol_cut, doctest::Approx(v1 - v2));
		CHECK_EQ(result.vol_cut12, doctest::Approx(0));
	}

	TEST_CASE("distinct objects don't overlap") {
		const auto s1 = cube_at(0, 0, 0, 4), s2 = cube_at(5, 5, 5, 4);

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE_EQ(result.status, intersect_status::distinct);
		WARN_EQ(result.vol_common, -1);
		WARN_EQ(result.vol_cut, -1);
		WARN_EQ(result.vol_cut12, -1);
	}

	TEST_CASE("objects touching") {
		int x = 0, y = 0, z = 0;

		SUBCASE("vertex") { x = y = z = 5; }
		SUBCASE("edge") { y = z = 5; }
		SUBCASE("face") { z = 5; }

		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(x, y, z, 5);

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE_EQ(result.status, intersect_status::touching);
	}

	TEST_CASE("objects near fuzzy value") {
		double z = 0;
		auto expected = intersect_status::touching;

		SUBCASE("overlap") {
			z = 4.4;
			expected = intersect_status::overlap;
		}
		SUBCASE("ok overlap") { z = 4.6; }
		SUBCASE("ok gap") { z = 5.4; }
		SUBCASE("distinct") {
			z = 5.6;
			expected = intersect_status::distinct;
		}

		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(0, 0, z, 5);

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE_EQ(result.status, expected);
	}
}
#endif

