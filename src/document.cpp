#include <cmath>
#include <cstdlib>
#include <string>

#include <spdlog/spdlog.h>

#include "document.hpp"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepAlgoAPI_Fuse.hxx>

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


/** determine whether a fuse operation did anything "interesting". i.e. does
 * the result consist of anything more complicated than just the union of the
 * two inputs?
 */
bool
is_trivial_union_fuse(BRepAlgoAPI_Fuse& op)
{
	assert(op.IsDone());

	/* we expect that fusing, i.e. taking the union, two non-overalapping
	 * shapes to just return a new shape that just references the
	 * original/input shapes
	 */

	// unforunately this .Shape() isn't const
	TopoDS_Iterator it{op.Shape()};

	// a union should contain exactly two shapes
	if (!it.More())
		return false;
	const TopoDS_Shape s1 = it.Value();
	it.Next();

	if (!it.More())
		return false;
	const TopoDS_Shape s2 = it.Value();
	it.Next();

	// should be done now
	if (it.More())
		return false;

	// make sure they are the same shapes
	return (
		(s1.IsEqual(op.Shape1()) && s2.IsEqual(op.Shape2())) ||
		(s1.IsEqual(op.Shape2()) && s2.IsEqual(op.Shape1())));
}


/** determine whether fusing resulted in one shape being "deleted" because it
 * was enclosed within the other
 *
 * this just replicates existing PPP code, but doesn't seem so useful, as it
 * would be good to know which one was the enclosing and which one was
 * removed!
 */
bool
is_enclosure_fuse(BRepAlgoAPI_Fuse& op)
{
	assert(op.IsDone());

	// previous PPP code just checks whether the volumes are the same! I think
	// we should be doing something better, but ah well
	const auto vol_s1 = volume_of_shape(op.Shape1());
	const auto vol_s2 = volume_of_shape(op.Shape2());
	const auto vol_res = volume_of_shape(op.Shape());

	const double drel = 1e-5;

	return (
		are_vals_close(vol_res, vol_s1, drel) ||
		are_vals_close(vol_res, vol_s2, drel));
}
