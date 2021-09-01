#include <vector>

// from opencascade
#include <TopoDS_Shape.hxx>


struct document {
	std::vector<TopoDS_Shape> solid_shapes;
	std::vector<TopoDS_Shape> shell_shapes;
	std::vector<TopoDS_Shape> compound_shapes;
	std::vector<TopoDS_Shape> other_shapes;

	// these just abort on errors, will do something better when it's clear
	// what that is!
	void load_brep_file(const char* path);
	void load_step_file(const char* path);

	void write_brep_file(const char* path);
};
