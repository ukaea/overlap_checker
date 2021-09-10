#ifdef INCLUDE_DOCTESTS
#include <doctest/doctest.h>
#endif

#include <spdlog/spdlog.h>

#include <BOPAlgo_PaveFiller.hxx>
#include <BOPAlgo_Operation.hxx>

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

#include <BRepTools.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <TopoDS_Builder.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_CompSolid.hxx>

#include <Message_Report.hxx>
#include <Message_Gravity.hxx>

#include "document.hpp"

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

