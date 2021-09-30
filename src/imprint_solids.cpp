#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

#include <TopoDS_Builder.hxx>
#include <TopoDS_Compound.hxx>

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
			spdlog::critical("CSV input does not contain two fields");
			return 1;
		}

		int first, second;
		if ((first = doc.lookup_solid(fields[0])) < 0) {
			spdlog::critical("first value ({}) is not a valid shape index", fields[0]);
			return 1;
		}

		if ((second = doc.lookup_solid(fields[1])) < 0) {
			spdlog::critical("second value ({}) is not a valid shape index", fields[1]);
			return 1;
		}

		const auto res = perform_solid_imprinting(
			doc.solid_shapes[first], doc.solid_shapes[second], 0.01);
		switch(res.status) {
		case imprint_status::failed:
			spdlog::error("{:5}-{:<5} failed to imprint", first, second);
			num_failed += 1;
			// continue because we don't want to put these shapes back into
			// the document!
			continue;

		case imprint_status::distinct:
			spdlog::debug("{:5}-{:<5} were mostly distinct", first, second);
			break;
		case imprint_status::merge_into_shape:
			spdlog::info(
				"{:5}-{:<5} were imprinted, a volume of {:.2f} was merged into {}",
				first, second, res.vol_common, first);
			break;
		case imprint_status::merge_into_tool:
			spdlog::info(
				"{:5}-{:<5} were imprinted, a volume of {:.2f} was merged into {}",
				first, second, res.vol_common, second);
			break;
		}
		doc.solid_shapes[first] = res.shape;
		doc.solid_shapes[second] = res.tool;
	}

	if (status != input_status::end_of_file) {
		spdlog::critical("failed to read line");
		return 1;
	}

	if (num_failed > 0) {
		spdlog::critical(
			"failed to imprint {} shapes",
			num_failed);
		return 1;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	configure_spdlog();

	std::string path_in, path_out;

	{
		CLI::App app{"Imprint touching and overlapping solids, write to BREP file."};
		app.add_option("input", path_in, "Path of the input file")
			->required()
			->option_text("input.brep");
		app.add_option("output", path_out, "Path of the output file")
			->required()
			->option_text("output.brep");
		CLI11_PARSE(app, argc, argv);
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
