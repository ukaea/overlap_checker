#include <cassert>
#include <string>
#include <utility>

#include <TopoDS_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include <BRepTools.hxx>

#include <cxx_argp_parser.h>
#include <aixlog.hpp>

#include "document.hpp"
#include "utils.hpp"

static int
merge_into(const document &doc, TopoDS_Compound &merged)
{
	TopoDS_Builder builder;
	builder.MakeCompound(merged);

	input_status status;
	std::vector<std::string> fields;
	while ((status = parse_next_row(std::cin, fields)) == input_status::success) {
		if (fields.size() < 2) {
			LOG(FATAL) << "CSV input does not contain two fields\n";
			return 1;
		}

		int first, second;
		if ((first = doc.lookup_solid(fields[0])) < 0) {
			LOG(FATAL) << "first value (" << fields[0] << ") is not a valid shape index\n";
			return 1;
		}

		if ((second = doc.lookup_solid(fields[1])) < 0) {
			LOG(FATAL) << "second value (" << fields[1] << ") is not a valid shape index\n";
			return 1;
		}

		std::stringstream hi_lo;
		hi_lo << std::setw(5) << first << '-' << std::left << second << std::right;

		LOG(INFO) << hi_lo.str() << " processing\n";

		boolean_op op{
			BOPAlgo_COMMON,
			doc.solid_shapes[first], doc.solid_shapes[second]};
		op.Build();
		if(!op.IsDone()) {
			LOG(FATAL) << "unable determine solid common to shapes\n";
			return 1;
		}

		builder.Add(merged, op.Shape());
	}

	if (status == input_status::end_of_file) {
		return 0;
	} else {
		LOG(FATAL) << "failed to read line\n";
		return 1;
	}
}

int
main(int argc, char **argv)
{
	configure_aixlog();

	std::string path_in, path_out;

	{
		const char *doc = "Collect overlapping areas of solids and write to BREP file.";
		const char *usage = "input.brep output.brep";

		cxx_argp::parser argp(2);
		if (!argp.parse(argc, argv, usage, doc)) {
			return 1;
		}

		const auto &args = argp.arguments();
		assert(args.size() == 2);
		path_in = args[0];
		path_out = args[1];
	}

	document doc;
	doc.load_brep_file(path_in.c_str());

	TopoDS_Compound merged;
	const int status = merge_into(doc, merged);
	if (status != 0) {
		return status;
	}

	LOG(DEBUG) << "writing brep file " << path_out << '\n';
	if (!BRepTools::Write(merged, path_out.c_str())) {
		LOG(FATAL) << "failed to write brep file\n";
		return 1;
	}

	return 0;
}
