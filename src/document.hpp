#include <vector>

class TDF_Label;

#include <TopoDS_Shape.hxx>


struct document {
	std::vector<TopoDS_Shape> solid_shapes;
	std::vector<TopoDS_Shape> shell_shapes;
	std::vector<TopoDS_Shape> compound_shapes;
	std::vector<TopoDS_Shape> other_shapes;

	bool load_brep_file(const char* path);
	bool load_step_file(const char* path);

	bool write_brep_file(const char* path);

	void summary();

	void add_xcaf_shape(const TDF_Label &label, const int depth=0);
};
