#ifdef INCLUDE_DOCTESTS
#include <doctest/doctest.h>
#endif

#include <spdlog/spdlog.h>

#include "BOPAlgo_Operation.hxx"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

#include <BOPAlgo_PaveFiller.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Common.hxx>

#include <BRepTools.hxx>
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
	const TopoDS_Shape& shape, const TopoDS_Shape& tool, double fuzzy_value,
	double &vol_common, double &vol_cut, double &vol_cut12)
{
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

	if (filler.HasWarnings()) {
		const Handle(Message_Report) report = filler.GetReport();

		const auto
			n_orig = report->GetAlerts(Message_Warning).Size();

		filler.SetFuzzyValue(0);
		filler.Perform();

		const auto
			n_new = report->GetAlerts(Message_Warning).Size();

		spdlog::info(
			"PaveFiller had {} warnings, fuzzy value set to {} giving {} warnings",
			n_orig, filler.FuzzyValue(), n_new);
	}

	// how should this be returned!
	assert(!filler.HasErrors());

	// I'm only using the Section class because it has the most convinient
	// constructor, the functionality mostly comes from
	// BRepAlgoAPI_BooleanOperation and BRepAlgoAPI_BuilderAlgo (at the time
	// of writing anyway!)
	BRepAlgoAPI_Section op{shape, tool, filler, false};
	op.SetOperation(BOPAlgo_COMMON);
	op.SetFuzzyValue(filler.FuzzyValue());
	op.SetNonDestructive(true);
	op.SetRunParallel(false);

	op.Build();
	assert(op.IsDone());

	TopExp_Explorer ex;
	ex.Init(op.Shape(), TopAbs_SOLID);
	if (ex.More()) {
		vol_common = volume_of_shape(op.Shape());

		op.SetOperation(BOPAlgo_CUT);
		op.Build();
		assert(op.IsDone());
		vol_cut = volume_of_shape(op.Shape());

		op.SetOperation(BOPAlgo_CUT21);
		op.Build();
		assert(op.IsDone());
		vol_cut12 = volume_of_shape(op.Shape());

		return intersect_result::overlap;
	}

	op.SetOperation(BOPAlgo_SECTION);
	op.Build();
	assert(op.IsDone());

	ex.Init(op.Shape(), TopAbs_VERTEX);
	if (ex.More()) {
		return intersect_result::touching;
	}

	return intersect_result::distinct;
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

		double vol_common = -1, vol_left = -1, vol_right = -1;
		const auto result = classify_solid_intersection(
			s1, s2, 0.5, vol_common, vol_left, vol_right);

		REQUIRE_EQ(result, intersect_result::overlap);
		CHECK_EQ(vol_common, doctest::Approx(10*10*10));
		CHECK_EQ(vol_left, doctest::Approx(0));
		CHECK_EQ(vol_right, doctest::Approx(0));
	}

	TEST_CASE("smaller object contained in larger one overlap") {
		const auto s1 = cube_at(0, 0, 0, 10), s2 = cube_at(2, 2, 2, 6);

		const double
			v1 = 10*10*10,
			v2 = 6*6*6;

		double vol_common = -1, vol_left = -1, vol_right = -1;
		const auto result = classify_solid_intersection(
			s1, s2, 0.5, vol_common, vol_left, vol_right);

		REQUIRE_EQ(result, intersect_result::overlap);
		CHECK_EQ(vol_common, doctest::Approx(v2));
		CHECK_EQ(vol_left, doctest::Approx(v1 - v2));
		CHECK_EQ(vol_right, doctest::Approx(0));
	}

	TEST_CASE("distinct objects don't overlap") {
		const auto s1 = cube_at(0, 0, 0, 4), s2 = cube_at(5, 5, 5, 4);

		double vol_common = -1, vol_left = -1, vol_right = -1;
		const auto result = classify_solid_intersection(
			s1, s2, 0.5, vol_common, vol_left, vol_right);

		REQUIRE_EQ(result, intersect_result::distinct);
		WARN_EQ(vol_common, -1);
		WARN_EQ(vol_left, -1);
		WARN_EQ(vol_right, -1);
	}

	TEST_CASE("objects touching") {
		int x = 0, y = 0, z = 0;

		SUBCASE("vertex") { x = y = z = 5; }
		SUBCASE("edge") { y = z = 5; }
		SUBCASE("face") { z = 5; }

		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(x, y, z, 5);

		double vol_common = -1, vol_left = -1, vol_right = -1;
		const auto result = classify_solid_intersection(
			s1, s2, 0.5, vol_common, vol_left, vol_right);

		REQUIRE_EQ(result, intersect_result::touching);
	}

	TEST_CASE("objects near fuzzy value") {
		double z = 0;
		auto expected = intersect_result::touching;

		SUBCASE("overlap") {
			z = 4.4;
			expected = intersect_result::overlap;
		}
		SUBCASE("ok overlap") { z = 4.6; }
		SUBCASE("ok gap") { z = 5.4; }
		SUBCASE("distinct") {
			z = 5.6;
			expected = intersect_result::distinct;
		}

		const auto s1 = cube_at(0, 0, 0, 5), s2 = cube_at(0, 0, z, 5);

		double vol_common = -1, vol_left = -1, vol_right = -1;
		const auto result = classify_solid_intersection(
			s1, s2, 0.5, vol_common, vol_left, vol_right);

		REQUIRE_EQ(result, expected);
	}
}
#endif
