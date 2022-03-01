#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <sys/types.h>

#ifdef INCLUDE_TESTS
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#endif

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

#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Message_ProgressScope.hxx>
#include <Message_Report.hxx>
#include <Message_Gravity.hxx>

#include <aixlog.hpp>

#include "geometry.hpp"
#include "utils.hpp"


std::ostream&
operator<<(std::ostream& str, TopAbs_ShapeEnum type)
{
	const char* name = "unknown";
	switch(type) {
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
	return str << name;
}

std::ostream&
operator<<(std::ostream& str, BRepCheck_Status status)
{
	const char* name = "unknown";
	switch(status)
	{
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
	return str << name;
}

static double
volume_of_shape_maybe_neg(const TopoDS_Shape& shape)
{
	GProp_GProps props;
	BRepGProp::VolumeProperties(shape, props);
	return props.Mass();
}

double
volume_of_shape(const TopoDS_Shape& shape)
{
	const double volume = volume_of_shape_maybe_neg(shape);
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

	LOG(FATAL) << "BRepExtrema_DistShapeShape::Perform() failed\n";
	dss.Dump(std::cerr);

	std::abort();
}

void
document::load_brep_file(const char* path)
{
	BRep_Builder builder;
	TopoDS_Shape shape;

	LOG(DEBUG) << "reading brep file " << path << '\n';

	if (!BRepTools::Read(shape, path, builder)) {
		LOG(FATAL) << "unable to read BREP file\n";
		std::exit(1);
	}

	switch(shape.ShapeType()) {
	case TopAbs_COMPOUND:
	case TopAbs_COMPSOLID:
		break;

	default:
		LOG(FATAL)
			<< "expected to get COMPOUND or COMPSOLID toplevel shape from brep file, not "
			<< shape.ShapeType()
			<< '\n';
		std::exit(1);
	}

	LOG(DEBUG) << "expecting " << shape.NbChildren() << " solid shapes\n";
	solid_shapes.reserve((size_t)shape.NbChildren());

	for (TopoDS_Iterator it(shape); it.More(); it.Next()) {
		const auto &shp = it.Value();
		switch (shp.ShapeType()) {
		case TopAbs_COMPOUND:
		case TopAbs_COMPSOLID:
		case TopAbs_SOLID:
			solid_shapes.push_back(shp);
			break;
		default:
			LOG(FATAL)
				<< "expecting shape to be a COMPSOLID or SOLID, not "
				<< shp.ShapeType()
				<< '\n';
			std::exit(1);
		}
	}
}

void
document::write_brep_file(const char* path) const
{
	LOG(DEBUG) << "merging " << solid_shapes.size() << " shapes for writing\n";

	TopoDS_Compound merged;
	TopoDS_Builder builder;
	builder.MakeCompound(merged);
	for (const auto &shape : solid_shapes) {
		builder.Add(merged, shape);
	}

	LOG(DEBUG) << "writing brep file " << path << '\n';

	if (!BRepTools::Write(merged, path)) {
		LOG(FATAL) << "failed to write brep file\n";
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

	LOG(WARNING)
		<< "shape " << i
		<< " contains following errors ";

	for (const auto status : checker.Result(shape)->Status()) {
		if (status != BRepCheck_NoError) {
			LOG(WARNING) << status;
		}
	}

	for (TopoDS_Iterator it{shape}; it.More(); it.Next()) {
		const auto &component = it.Value();

		if (checker.IsValid(component)) {
			continue;
		}

		for (const auto status : checker.Result(component)->Status()) {
			if (status != BRepCheck_NoError) {
				LOG(WARNING) << status;
			}
		}
	}

	LOG(WARNING) << '\n';

	return false;
}

size_t
document::count_invalid_shapes() const
{
	int i = 0;
	size_t num_invalid = 0;
	for (const auto &shape : solid_shapes) {
		LOG(DEBUG) << "checking shape" << i << '\n';
		if (!is_shape_valid(i, shape)) {
			num_invalid += 1;
		}
		i++;
	}
	return num_invalid;
}

ssize_t
document::lookup_solid(const std::string &str) const
{
	int idx = -1;
	if (!int_of_string(str.c_str(), idx)) {
		return -1;
	}
	if (idx < 0 || (size_t)idx >= solid_shapes.size()) {
		return -1;
	}
	return (ssize_t)idx;
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

class ProgressTimeout : public Message_ProgressIndicator {
	typedef std::chrono::steady_clock clock;
	std::chrono::time_point<clock> startedat_, expireat_;
	std::unique_ptr<Message_ProgressScope> scope_;
	bool expired_;

public:
	ProgressTimeout() : expired_{false} {}

	void begin(BOPAlgo_Algo &algo, unsigned timeout_millisecs) {
		startedat_ = clock::now();
		if (timeout_millisecs > 0) {
			scope_ = std::make_unique<Message_ProgressScope>(Start(), nullptr, 0);
			expireat_ = startedat_ + std::chrono::milliseconds{timeout_millisecs};
			algo.SetProgressIndicator(*scope_);
		}
	}

	bool expired() const {
		return expired_;
	}

	double duration_secs() const {
		return std::chrono::duration<double>(clock::now() - startedat_).count();
	}

	void Show (const Message_ProgressScope &, const Standard_Boolean) override { }

	Standard_Boolean UserBreak() override {
		if (expired_) {
			return true;
		} else if (clock::now() < expireat_) {
			return false;
		} else {
			expired_ = true;
			return true;
		}
	}
};


intersect_result classify_solid_intersection(
	const TopoDS_Shape& shape, const TopoDS_Shape& tool,
	double fuzzy_value, unsigned pave_time_millisecs)
{
	intersect_result result = {
		intersect_status::failed,
		// fuzzy value
		0.0,
		// number of warnings
		0, 0, 0,
		// volumes
		-1.0, -1.0, -1.0,
		// pave time
		-1.0,
	};

	// create here as they need a longer scope than the pave filler
	ProgressTimeout timeout;

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

	timeout.begin(filler, pave_time_millisecs);

	// this can be a very expensive call, e.g. 10+ seconds
	filler.Perform();

	result.pave_time_seconds = timeout.duration_secs();
	result.fuzzy_value = filler.FuzzyValue();

	{
		Handle(Message_Report) report = filler.GetReport();
		collect_warnings(report.get(), result.num_filler_warnings);
		// this report is merged into the following reports
		report->Clear();
	}

	if (timeout.expired()) {
		result.status = intersect_status::timeout;
		return result;
	}

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
		// OCCT (version 7.5) appears to occasionally come back with a
		// negative volume. it appears to do this when the two solids have
		// non-trivial faces that are within the given tolerance/fuzzy value
		result.vol_common = volume_of_shape_maybe_neg(op.Shape());

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

		if (result.vol_common < 0) {
			// ensure the this negative volume is "small", relative to the
			// input shapes, as we only expect this to happen along the
			// boundary of shapes
			const double limit = std::min(result.vol_cut, result.vol_cut12) * 0.1;
			if (limit < -result.vol_common) {
				throw std::runtime_error("negative volume too large");
			}

			// until this is fixed upstream in OCCT, recording them as
			// touching seems to be best. an alternative would be to fail, and
			// let the caller retry with stricter tolerance (which tends to
			// succeed). touching seems best as we later steps want to know
			// which solids are close to each other and therefore need to
			// considered during merging
			result.status = intersect_status::touching;
		} else {
			result.status = intersect_status::overlap;
		}
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

#ifdef INCLUDE_TESTS
#include <BRepPrimAPI_MakeBox.hxx>

static inline TopoDS_Shape
cube_at(double x, double y, double z, double length)
{
	return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), length, length, length).Shape();
}

TEST_CASE("classify_solid_intersection") {
	using Catch::Approx;

	SECTION("two identical objects completely overlap") {
		const auto s1 = cube_at(0, 0, 0, 10), s2 = cube_at(0, 0, 0, 10);

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE(result.status == intersect_status::overlap);
		CHECK(result.vol_common == Approx(10*10*10));
		CHECK(result.vol_cut == Approx(0));
		CHECK(result.vol_cut12 == Approx(0));
	}

	SECTION("smaller object contained in larger one overlap") {
		const auto s1 = cube_at(0, 0, 0, 10), s2 = cube_at(2, 2, 2, 6);

		const double
			v1 = 10*10*10,
			v2 = 6*6*6;

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE(result.status == intersect_status::overlap);
		CHECK(result.vol_common == Approx(v2));
		CHECK(result.vol_cut == Approx(v1 - v2));
		CHECK(result.vol_cut12 == Approx(0));
	}

	SECTION("distinct objects don't overlap") {
		const auto s1 = cube_at(0, 0, 0, 4), s2 = cube_at(5, 5, 5, 4);

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE(result.status == intersect_status::distinct);
		CHECK(result.vol_common == -1);
		CHECK(result.vol_cut == -1);
		CHECK(result.vol_cut12 == -1);
	}

	SECTION("objects touching") {
		int x = 0, y = 0, z = 0;

		SECTION("vertex") { x = y = z = 5; }
		SECTION("edge") { y = z = 5; }
		SECTION("face") { z = 5; }

		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(x, y, z, 5);

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE(result.status == intersect_status::touching);
	}

	SECTION("objects near fuzzy value") {
		double z = 0;
		auto expected = intersect_status::touching;

		SECTION("overlap") {
			z = 4.4;
			expected = intersect_status::overlap;
		}
		SECTION("ok overlap") { z = 4.6; }
		SECTION("ok gap") { z = 5.4; }
		SECTION("distinct") {
			z = 5.6;
			expected = intersect_status::distinct;
		}

		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(0, 0, z, 5);

		const auto result = classify_solid_intersection(s1, s2, 0.5);

		REQUIRE(result.status == expected);
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

#ifdef INCLUDE_TESTS
#include <BRepPrimAPI_MakeBox.hxx>

TEST_CASE("perform_solid_imprinting") {
	using Catch::Approx;

	SECTION("two identical objects") {
		const auto s1 = cube_at(0, 0, 0, 10), s2 = cube_at(0, 0, 0, 10);

		const auto res = perform_solid_imprinting(s1, s2, 0.5);

		switch (res.status) {
		case imprint_status::merge_into_shape:
			CHECK(volume_of_shape(res.shape) == Approx(10*10*10));
			CHECK(volume_of_shape(res.tool) == Approx(0));
			break;
		case imprint_status::merge_into_tool:
			CHECK(volume_of_shape(res.shape) == Approx(0));
			CHECK(volume_of_shape(res.tool) == Approx(10*10*10));
			break;
		default:
			// unreachable if working
			REQUIRE(false);
		}

		CHECK(res.vol_common == Approx(10*10*10));
		CHECK(res.vol_cut == 0);
		CHECK(res.vol_cut12 == 0);
	}

	SECTION("two independent objects") {
		const auto s1 = cube_at(0, 0, 0, 4), s2 = cube_at(5, 0, 0, 4);

		const auto res = perform_solid_imprinting(s1, s2, 0.5);
		REQUIRE(res.status == imprint_status::distinct);

		CHECK(res.vol_common == 0);
		CHECK(res.vol_cut == Approx(4*4*4));
		CHECK(res.vol_cut12 == Approx(4*4*4));

		CHECK(volume_of_shape(res.shape) == Approx(4*4*4));
		CHECK(volume_of_shape(res.tool) == Approx(4*4*4));
	}

	SECTION("two touching objects") {
		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(5, 0, 0, 5);

		const auto res = perform_solid_imprinting(s1, s2, 0.5);
		REQUIRE(res.status == imprint_status::distinct);

		CHECK(res.vol_common == 0);
		CHECK(res.vol_cut == Approx(5*5*5));
		CHECK(res.vol_cut12 == Approx(5*5*5));

		CHECK(volume_of_shape(res.shape) == Approx(5*5*5));
		CHECK(volume_of_shape(res.tool) == Approx(5*5*5));
	}

	SECTION("two objects overlapping at corner") {
		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(4, 4, 4, 2);

		const auto res = perform_solid_imprinting(s1, s2, 0.1);
		REQUIRE(res.status == imprint_status::merge_into_shape);

		CHECK(res.vol_common == Approx(1));
		CHECK(res.vol_cut == Approx(5*5*5-1));
		CHECK(res.vol_cut12 == Approx(2*2*2-1));

		CHECK(volume_of_shape(res.shape) == Approx(5*5*5));
		CHECK(volume_of_shape(res.tool) == Approx(2*2*2-1));
	}

	SECTION("two objects overlapping in middle") {
		// s1 should divide s2 in half, one of these halves should be merged
		// into s1
		const auto s1 = cube_at(3, 1, 1, 2), s2 = cube_at(0, 0, 0, 4);

		const auto res = perform_solid_imprinting(s1, s2, 0.1);
		REQUIRE(res.status == imprint_status::merge_into_tool);

		const double half_s1 = 2*2*2 / 2.;
		CHECK(res.vol_common == Approx(half_s1));
		CHECK(res.vol_cut == Approx(half_s1));
		CHECK(res.vol_cut12 == Approx(4*4*4 - half_s1));

		CHECK(volume_of_shape(res.shape) == Approx(half_s1));
		CHECK(volume_of_shape(res.tool) == Approx(4*4*4));
	}
}

#endif
