#include <array>
#include <assert.h>
#include <sstream>

#include <spdlog/spdlog.h>

#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Iterator.hxx>

#include <TopExp_Explorer.hxx>

#include <BRepPrimAPI_MakeBox.hxx>

#include <BRepTools.hxx>
#include <BRep_Tool.hxx>

#include <BOPAlgo_PaveFiller.hxx>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_BuilderAlgo.hxx>
#include <BRepAlgoAPI_Section.hxx>

#include "BRepAlgoAPI_BooleanOperation.hxx"
#include "BRepAlgoAPI_Common.hxx"
#include "BRepAlgoAPI_Cut.hxx"
#include "TopAbs_ShapeEnum.hxx"

#include "document.hpp"


static void
test_bops()
{
	const TopoDS_Shape
		s1 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 10), 10, 10, 10).Shape(),
		s2 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 20), 10, 10, 10).Shape();

	// explicitly construct a PaveFiller so we can reuse the work between
	// operations, at a minimum we want to perform sectioning and getting any
	// common solid
	BOPAlgo_PaveFiller filler;

	{
		TopTools_ListOfShape args;
		args.Append(s1);
		args.Append(s2);
		filler.SetArguments(args);
	}

	filler.SetRunParallel(false);
	filler.SetFuzzyValue(0.5);
	filler.SetNonDestructive(true);

	filler.Perform();
	assert(!filler.HasErrors());

	// I'm only using the Section class because it has the most convinient set
	// of methods, most of the functionality actually comes from
	// BRepAlgoAPI_BooleanOperation and BRepAlgoAPI_BuilderAlgo (at the time
	// of writing anyway!)
	BRepAlgoAPI_Section op(s1, s2, filler, false);
	op.SetFuzzyValue(0.5);
	op.SetNonDestructive(true);
	op.SetRunParallel(false);

	op.Build();
	assert(op.IsDone());
	assert(BRepTools::Write(op.Shape(), "section.brep"));

	TopExp_Explorer ex;
	ex.Init(op.Shape(), TopAbs_EDGE);
	bool section_has_edges = ex.More();
	ex.Init(op.Shape(), TopAbs_VERTEX);
	bool section_has_verts = ex.More();

	spdlog::info(
		"section operation, has modified={}, edges={} verticies={}",
		op.HasModified(), section_has_edges, section_has_verts);

	op.SetOperation(BOPAlgo_COMMON);
	op.Build();
	assert(op.IsDone());
	assert(BRepTools::Write(op.Shape(), "common.brep"));

	ex.Init(op.Shape(), TopAbs_SOLID);
	bool common_has_solids = ex.More();
	ex.Init(op.Shape(), TopAbs_FACE);
	bool common_has_faces = ex.More();
	ex.Init(op.Shape(), TopAbs_EDGE);
	bool common_has_edges = ex.More();
	ex.Init(op.Shape(), TopAbs_VERTEX);
	bool common_has_verts = ex.More();

	spdlog::info(
		"common operation, has modified={} solids={} faces={} edges={} verticies={}",
		op.HasModified(), common_has_solids, common_has_faces, common_has_edges, common_has_verts);

	op.SetOperation(BOPAlgo_CUT);
	op.Build();
	assert(op.IsDone());
	assert(BRepTools::Write(op.Shape(), "cut.brep"));

	op.SetOperation(BOPAlgo_CUT21);
	op.Build();
	assert(op.IsDone());
	assert(BRepTools::Write(op.Shape(), "cut21.brep"));


	spdlog::info(
		"shape vols {:.1f} {:.1f}",
		volume_of_shape(s1), volume_of_shape(s2));

	double vol_common = 0, vol_left = 0, vol_right = 0;
	const auto result = classify_solid_intersection(
		s1, s2, 0.5, vol_common, vol_left, vol_right);

	spdlog::info(
		"classification = {}, vols = {:.1f} {:.1f} {:.1f}",
		result, vol_common, vol_left, vol_right);
}

int
main()
{
	test_bops();

	return 0;
}
