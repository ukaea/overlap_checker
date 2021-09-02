#include <assert.h>
#include <iostream>

#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Iterator.hxx>

#include <TopExp_Explorer.hxx>

#include <BRep_Tool.hxx>

#include <BRepTools.hxx>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_BuilderAlgo.hxx>

#include "document.hpp"



int
main()
{
	const TopoDS_Shape
		s1 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 10), 10, 10, 10).Shape(),
		s2 = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 10), 10, 10, 10).Shape();

	BRepAlgoAPI_Fuse fuser(s1, s2);
	fuser.SetNonDestructive(true);
	fuser.SetRunParallel(false);

	fuser.Build();
	assert(fuser.IsDone());

	assert (BRepTools::Write(fuser.Shape(), "tmp.brep"));

	fuser.History()->Dump(std::cout);

	return 0;
}
