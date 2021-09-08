#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

#include <spdlog/spdlog.h>

#include "BOPAlgo_Operation.hxx"
#include "document.hpp"

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
	const TopoDS_Shape& shape1, const TopoDS_Shape& shape2, double fuzzy_value,
	double &vol_common, double &vol_left, double &vol_right)
{
	// explicitly construct a PaveFiller so we can reuse the work between
	// operations, at a minimum we want to perform sectioning and getting any
	// common solid
	BOPAlgo_PaveFiller filler;

	{
		TopTools_ListOfShape args;
		args.Append(shape1);
		args.Append(shape2);
		filler.SetArguments(args);
	}

	filler.SetRunParallel(false);
	filler.SetFuzzyValue(fuzzy_value);
	filler.SetNonDestructive(true);

	filler.Perform();

	// how should this be returned!
	assert(!filler.HasErrors());

	// I'm only using the Section class because it has the most convinient
	// constructor, the functionality mostly comes from
	// BRepAlgoAPI_BooleanOperation and BRepAlgoAPI_BuilderAlgo (at the time
	// of writing anyway!)
	BRepAlgoAPI_Section op{shape1, shape2, filler, false};
	op.SetOperation(BOPAlgo_COMMON);
	op.SetFuzzyValue(fuzzy_value);
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
		vol_left = volume_of_shape(op.Shape());

		op.SetOperation(BOPAlgo_CUT21);
		op.Build();
		assert(op.IsDone());
		vol_right = volume_of_shape(op.Shape());

		return intersect_result::overlaps;
	}

	op.SetOperation(BOPAlgo_SECTION);
	op.Build();
	assert(op.IsDone());

	ex.Init(op.Shape(), TopAbs_VERTEX);
	if (ex.More()) {
		return intersect_result::touches;
	}

	return intersect_result::nothing;
}
