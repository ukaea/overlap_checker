#include <cstdlib>
#include <stdexcept>

#ifdef INCLUDE_DOCTESTS
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <BOPAlgo_PaveFiller.hxx>
#include <BOPAlgo_Operation.hxx>

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

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

double
volume_of_shape(const TopoDS_Shape& shape)
{
	GProp_GProps props;
	BRepGProp::VolumeProperties(shape, props);
	const double volume = props.Mass();
	if (volume < 0) {
		throw std::runtime_error("volume of shape less than zero");
	}
	return volume;
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

	spdlog::debug("reading brep file {}", path);

	if (!BRepTools::Read(shape, path, builder)) {
		spdlog::critical("unable to read BREP file");
		std::exit(1);
	}

	if (shape.ShapeType() != TopAbs_COMPOUND && shape.ShapeType() != TopAbs_COMPSOLID) {
		spdlog::critical(
			"expected to get COMPOUND or COMPSOLID toplevel shape from brep file, not {}",
			shape.ShapeType());
		std::exit(1);
	}

	spdlog::debug("expecting {} solid shapes", shape.NbChildren());
	solid_shapes.reserve(shape.NbChildren());

	for (TopoDS_Iterator it(shape); it.More(); it.Next()) {
		const auto &shp = it.Value();
		if (shp.ShapeType() != TopAbs_COMPSOLID && shp.ShapeType() != TopAbs_SOLID) {
			spdlog::critical(
				"expecting shape to be a COMPSOLID or SOLID, not {}",
				shp.ShapeType());
			std::exit(1);
		}

		solid_shapes.push_back(shp);
	}
}

void
document::write_brep_file(const char* path) const
{
	spdlog::debug("merging {} shapes for writing", solid_shapes.size());

	TopoDS_Compound merged;
	TopoDS_Builder builder;
	builder.MakeCompound(merged);
	for (const auto &shape : solid_shapes) {
		builder.Add(merged, shape);
	}

	spdlog::debug("writing brep file {}", path);

	if (!BRepTools::Write(merged, path)) {
		spdlog::critical("failed to write brep file");
		std::exit(1);
	}
}

static bool
is_shape_valid(int i, const TopoDS_Shape& shape)
{
	BRepCheck_Analyzer checker{shape};
	if (checker.IsValid()) {
		return true;
	}

	std::vector<BRepCheck_Status> errors;

	for (const auto status : checker.Result(shape)->Status()) {
		if (status != BRepCheck_NoError) {
			errors.push_back(status);
		}
	}

	for (TopoDS_Iterator it{shape}; it.More(); it.Next()) {
		const auto &component = it.Value();

		if (checker.IsValid(component)) {
			continue;
		}

		for (const auto status : checker.Result(component)->Status()) {
			if (status != BRepCheck_NoError) {
				errors.push_back(status);
			}
		}
	}

	spdlog::warn(
		"shape {} contains following errors {}",
		i, errors);

	return false;
}

size_t
document::count_invalid_shapes() const
{
	int i = 0;
	size_t num_invalid = 0;
	for (const auto &shape : solid_shapes) {
		spdlog::debug("checking shape {}", i);
		if (!is_shape_valid(i, shape)) {
			num_invalid += 1;
		}
		i++;
	}
	return num_invalid;
}

int
document::lookup_solid(const std::string &str) const
{
	int idx = -1;
	if (!int_of_string(str.c_str(), idx)) {
		return -1;
	}
	if (idx < 0 || (size_t)idx >= solid_shapes.size()) {
		return -1;
	}
	return idx;
}


static inline bool
collect_warnings(const Message_Report *report, int &warnings)
{
	if (report) {
		warnings = report->GetAlerts(Message_Warning).Size();
		return true;
	} else {
		return false;
	}
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
	filler.SetRunParallel(false);
	filler.SetFuzzyValue(fuzzy_value);
	filler.SetNonDestructive(true);

	{
		TopTools_ListOfShape args;
		args.Append(shape);
		args.Append(tool);
		filler.SetArguments(args);
	}

	// this can be a very expensive call, e.g. 10+ seconds
	filler.Perform();

	{
		Handle(Message_Report) report = filler.GetReport();
		collect_warnings(report.get(), result.num_filler_warnings);
		// this report is merged into the following reports
		report->Clear();
	}

	result.fuzzy_value = filler.FuzzyValue();

	if (filler.HasErrors()) {
		return result;
	}

	boolean_op op{filler, BOPAlgo_COMMON, shape, tool};
	op.SetFuzzyValue(filler.FuzzyValue());
	op.Build();
	collect_warnings(op.GetReport().get(), result.num_common_warnings);
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
	collect_warnings(op.GetReport().get(), result.num_section_warnings);
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

TEST_SUITE("classify_solid_intersection") {
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

static inline bool shape_has_verticies(TopoDS_Shape shape)
{
	TopExp_Explorer ex;
	ex.Init(shape, TopAbs_VERTEX);
	return ex.More();
}

imprint_result perform_solid_imprinting(
	const TopoDS_Shape& shape, const TopoDS_Shape& tool, double fuzzy_value)
{
	imprint_result result = {
		imprint_status::failed,
		// fuzzy value
		0.0,
		// number of warnings
		0, 0, 0,
		// volumes
		-1.0, -1.0, -1.0,
		// shapes
		{}, {},
	};

	BOPAlgo_PaveFiller filler;
	filler.SetRunParallel(false);
	filler.SetFuzzyValue(fuzzy_value);
	filler.SetNonDestructive(true);

	{
		TopTools_ListOfShape args;
		args.Append(shape);
		args.Append(tool);
		filler.SetArguments(args);
	}

	// this can be a very expensive call, e.g. 10+ seconds
	filler.Perform();

	{
		Handle(Message_Report) report = filler.GetReport();
		collect_warnings(report.get(), result.num_filler_warnings);
		// this report is merged into the following reports
		report->Clear();
	}

	result.fuzzy_value = filler.FuzzyValue();
	if (filler.HasErrors()) {
		return result;
	}

	TopoDS_Shape common;

	{
		boolean_op op{filler, BOPAlgo_COMMON, shape, tool};
		op.SetFuzzyValue(filler.FuzzyValue());
		op.Build();
		collect_warnings(op.GetReport().get(), result.num_common_warnings);
		if (op.HasErrors()) {
			return result;
		}
		common = op.Shape();
		result.vol_common = volume_of_shape(common);

		op.SetOperation(BOPAlgo_CUT);
		op.Build();
		if (op.HasErrors()) {
			return result;
		}
		result.shape = op.Shape();
		result.vol_cut = volume_of_shape(result.shape);

		op.SetOperation(BOPAlgo_CUT21);
		op.Build();
		if (op.HasErrors()) {
			return result;
		}
		result.tool = op.Shape();
		result.vol_cut12 = volume_of_shape(result.tool);
	}

	if (!shape_has_verticies(common)) {
		result.status = imprint_status::distinct;
	} else {
		// merge the common volume into the larger shape
		const bool merge_into_shape = result.vol_cut >= result.vol_cut12;

		boolean_op op{
			BOPAlgo_FUSE,
			merge_into_shape ? result.shape : result.tool,
			common
		};
		// fuzzy stuff has already been done so no need to introduce more error
		// op.SetFuzzyValue(filler.FuzzyValue());
		// the above created distinct shapes, so we are free to modify here

		op.Build();
		collect_warnings(op.GetReport().get(), result.num_fuse_warnings);
		if (op.HasErrors()) {
			return result;
		}

		if (merge_into_shape) {
			result.status = imprint_status::merge_into_shape;
			result.shape = op.Shape();
		} else {
			result.status = imprint_status::merge_into_tool;
			result.tool = op.Shape();
		}
	}

	return result;
}

#ifdef DOCTEST_LIBRARY_INCLUDED
#include <BRepPrimAPI_MakeBox.hxx>

TEST_SUITE("perform_solid_imprinting") {
	TEST_CASE("two identical objects") {
		const auto s1 = cube_at(0, 0, 0, 10), s2 = cube_at(0, 0, 0, 10);

		const auto res = perform_solid_imprinting(s1, s2, 0.5);

		switch (res.status) {
		case imprint_status::merge_into_shape:
			CHECK_EQ(volume_of_shape(res.shape), doctest::Approx(10*10*10));
			CHECK_EQ(volume_of_shape(res.tool), doctest::Approx(0));
			break;
		case imprint_status::merge_into_tool:
			CHECK_EQ(volume_of_shape(res.shape), doctest::Approx(0));
			CHECK_EQ(volume_of_shape(res.tool), doctest::Approx(10*10*10));
			break;
		default:
			// unreachable if working
			REQUIRE(false);
		}

		CHECK_EQ(res.vol_common, doctest::Approx(10*10*10));
		CHECK_EQ(res.vol_cut, 0);
		CHECK_EQ(res.vol_cut12, 0);
	}

	TEST_CASE("two independent objects") {
		const auto s1 = cube_at(0, 0, 0, 4), s2 = cube_at(5, 0, 0, 4);

		const auto res = perform_solid_imprinting(s1, s2, 0.5);
		REQUIRE_EQ(res.status, imprint_status::distinct);

		CHECK_EQ(res.vol_common, 0);
		CHECK_EQ(res.vol_cut, doctest::Approx(4*4*4));
		CHECK_EQ(res.vol_cut12, doctest::Approx(4*4*4));

		CHECK_EQ(volume_of_shape(res.shape), doctest::Approx(4*4*4));
		CHECK_EQ(volume_of_shape(res.tool), doctest::Approx(4*4*4));
	}

	TEST_CASE("two touching objects") {
		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(5, 0, 0, 5);

		const auto res = perform_solid_imprinting(s1, s2, 0.5);
		REQUIRE_EQ(res.status, imprint_status::distinct);

		CHECK_EQ(res.vol_common, 0);
		CHECK_EQ(res.vol_cut, doctest::Approx(5*5*5));
		CHECK_EQ(res.vol_cut12, doctest::Approx(5*5*5));

		CHECK_EQ(volume_of_shape(res.shape), doctest::Approx(5*5*5));
		CHECK_EQ(volume_of_shape(res.tool), doctest::Approx(5*5*5));
	}

	TEST_CASE("two objects overlapping at corner") {
		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(4, 4, 4, 2);

		const auto res = perform_solid_imprinting(s1, s2, 0.1);
		REQUIRE_EQ(res.status, imprint_status::merge_into_shape);

		CHECK_EQ(res.vol_common, doctest::Approx(1));
		CHECK_EQ(res.vol_cut, doctest::Approx(5*5*5-1));
		CHECK_EQ(res.vol_cut12, doctest::Approx(2*2*2-1));

		CHECK_EQ(volume_of_shape(res.shape), doctest::Approx(5*5*5));
		CHECK_EQ(volume_of_shape(res.tool), doctest::Approx(2*2*2-1));
	}

	TEST_CASE("two objects overlapping in middle") {
		// s1 should divide s2 in half, one of these halves should be merged
		// into s1
		const auto s1 = cube_at(3, 1, 1, 2), s2 = cube_at(0, 0, 0, 4);

		const auto res = perform_solid_imprinting(s1, s2, 0.1);
		REQUIRE_EQ(res.status, imprint_status::merge_into_tool);

		const double half_s1 = 2*2*2 / 2.;
		CHECK_EQ(res.vol_common, doctest::Approx(half_s1));
		CHECK_EQ(res.vol_cut, doctest::Approx(half_s1));
		CHECK_EQ(res.vol_cut12, doctest::Approx(4*4*4 - half_s1));

		CHECK_EQ(volume_of_shape(res.shape), doctest::Approx(half_s1));
		CHECK_EQ(volume_of_shape(res.tool), doctest::Approx(4*4*4));
	}
}

#endif
