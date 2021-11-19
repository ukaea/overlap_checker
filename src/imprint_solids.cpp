#include <cassert>
#include <iomanip>
#include <ios>
#include <string>
#include <utility>

#include <TopoDS_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include <cxx_argp_parser.h>
#include <aixlog.hpp>

#include "document.hpp"
#include "utils.hpp"


static int
imprint(document &doc)
{
	int num_failed = 0;

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

		const auto res = perform_solid_imprinting(
			doc.solid_shapes[first], doc.solid_shapes[second], 0.01);
		switch(res.status) {
		case imprint_status::failed:
			LOG(ERROR) << hi_lo.str() << " failed to imprint\n";
			num_failed += 1;
			// continue because we don't want to put these shapes back into
			// the document!
			continue;
		case imprint_status::distinct:
			LOG(DEBUG) << hi_lo.str() << " were mostly distinct\n";
			break;
		case imprint_status::merge_into_shape:
			LOG(INFO)
				<< hi_lo.str() << " were imprinted, "
				<< "a volume of " << std::fixed << std::setprecision(2) << res.vol_common
				<< "was merged into " << first << '\n';
			break;
		case imprint_status::merge_into_tool:
			LOG(INFO)
				<< hi_lo.str() << " were imprinted, "
				<< "a volume of " << std::fixed << std::setprecision(2) << res.vol_common
				<< "was merged into " << second << '\n';
			break;
		}
		doc.solid_shapes[first] = res.shape;
		doc.solid_shapes[second] = res.tool;
	}

	if (status != input_status::end_of_file) {
		LOG(FATAL) << "failed to read line\n";
		return 1;
	}

	if (num_failed > 0) {
		LOG(FATAL) << "failed to imprint " << num_failed << " shapes\n";
		return 1;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	configure_aixlog();

	std::string path_in, path_out;

	{
		const char *doc = (
			"Perform imprinting of touching and overlapping solids, writing results to BREP file.\n"
			"\n"
			"The intersection of any overlapping shapes will be assigned to the one with a larger volume.");
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

	const int status = imprint(doc);
	if (status != 0) {
		return status;
	}

	doc.write_brep_file(path_out.c_str());

	return 0;
}
