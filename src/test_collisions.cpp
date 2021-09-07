#include <assert.h>
#include <iostream>

#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Iterator.hxx>

#include <TopExp_Explorer.hxx>

#include <BRep_Tool.hxx>

#include <BRepTools.hxx>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_BuilderAlgo.hxx>

#include "document.hpp"


void
test_trivial_union()
{
	const TopoDS_Shape
		s1 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 10), 10, 10, 10).Shape(),
		s2 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 30), 10, 10, 10).Shape();

	BRepAlgoAPI_Fuse fuser(s1, s2);
	fuser.SetNonDestructive(true);
	fuser.SetRunParallel(false);

	fuser.Build();

	assert(is_trivial_union_fuse(fuser));
}

void
test_enclosed()
{
	const TopoDS_Shape
		s1 = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 20, 20, 20).Shape(),
		s2 = BRepPrimAPI_MakeBox(gp_Pnt(5, 5, 5), 10, 10, 10).Shape();

	BRepAlgoAPI_Fuse fuser(s1, s2);
	fuser.SetNonDestructive(true);
	fuser.SetRunParallel(false);

	fuser.Build();

	assert(is_enclosure_fuse(fuser));
}

void
test_coincidence()
{
	const TopoDS_Shape
		s1 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 10), 10, 10, 11).Shape(),
		s2 = BRepPrimAPI_MakeBox(gp_Pnt(11, 10, 20), 10, 10, 10).Shape();

	BRepAlgoAPI_Fuse fuser(s1, s2);
	fuser.SetNonDestructive(true);
	fuser.SetRunParallel(false);

	fuser.Build();

	assert(fuser.IsDone());

	assert(BRepTools::Write(fuser.Shape(), "tmp.brep"));

	fuser.History()->Dump(std::cerr);

	TopoDS_Iterator it{fuser.Shape()};

	assert(it.More());
	const TopoDS_Shape t1 = it.Value();
	it.Next();

	assert(it.More());
	const TopoDS_Shape t2 = it.Value();
	it.Next();

	assert (!it.More());
}

int
main()
{
	test_trivial_union();
	test_enclosed();
	test_coincidence();

	return 0;
}
