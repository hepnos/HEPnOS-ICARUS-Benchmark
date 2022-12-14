#include <mpi.h>
#include <iostream>
#include <sstream>
#include <regex>
#include <fstream>
#include <string>
#include <random>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <tclap/CmdLine.h>
#include <hepnos.hpp>
#include "DummyProduct.hpp"

static int                       g_size;
static int                       g_rank;
static std::string               g_protocol;
static std::string               g_connection_file;
static std::string               g_margo_file;
static std::string               g_input_dataset;
static std::string               g_product_label;
static std::vector<size_t>       g_product_sizes;
static spdlog::level::level_enum g_logging_level;
static unsigned                  g_num_threads;
static std::pair<double,double>  g_wait_range;
static std::mt19937              g_mte;

static void parse_arguments(int argc, char** argv);
static std::pair<double,double> parse_wait_range(const std::string&);
static std::string check_file_exists(const std::string& filename);
static std::vector<size_t> parse_product_sizes(const std::string&);
static void run_benchmark();

int main(int argc, char** argv) {

    int provided, required = MPI_THREAD_MULTIPLE;
    MPI_Init_thread(&argc, &argv, required, &provided);
    MPI_Comm_size(MPI_COMM_WORLD, &g_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &g_rank);

    std::stringstream str_format;
    str_format << "[" << std::setw(6) << std::setfill('0') << g_rank << "|" << g_size
               << "] [%H:%M:%S.%F] [%n] [%^%l%$] %v";
    spdlog::set_pattern(str_format.str());

    parse_arguments(argc, argv);

    spdlog::set_level(g_logging_level);

    if(provided != required && g_rank == 0) {
        spdlog::warn("MPI doesn't provider MPI_THREAD_MULTIPLE");
    }

    spdlog::trace("connection file: {}", g_connection_file);
    spdlog::trace("input dataset: {}", g_input_dataset);
    spdlog::trace("product label: {}", g_product_label);
    spdlog::trace("num threads: {}", g_num_threads);
    spdlog::trace("wait range: {},{}", g_wait_range.first, g_wait_range.second);

    MPI_Barrier(MPI_COMM_WORLD);

    spdlog::trace("Initializing RNG");
    g_mte = std::mt19937(g_rank);

    run_benchmark();

    MPI_Finalize();
    return 0;
}

static void parse_arguments(int argc, char** argv) {
    try {
        TCLAP::CmdLine cmd("Benchmark HEPnOS Parallel Event Processor", ' ', "0.6");
        // mandatory arguments
        TCLAP::ValueArg<std::string> protocol("p", "protocol",
            "Mercury protocol", true, "", "string");
        TCLAP::ValueArg<std::string> clientFile("c", "connection",
            "YAML connection file for HEPnOS", true, "", "string");
        TCLAP::ValueArg<std::string> dataSetName("d", "dataset",
            "DataSet from which to load the data", true, "", "string");
        TCLAP::ValueArg<std::string> productLabel("l", "label",
            "Label to use when storing products", true, "", "string");
        TCLAP::ValueArg<std::string> productSizes("s", "product-sizes",
            "Comma-separated product sizes (e.g. 45,67,123)", true, "", "string");
        // optional arguments
        TCLAP::ValueArg<std::string> margoFile("m", "margo-config",
            "Margo configuration file", false, "", "string");
        std::vector<std::string> allowed = {
            "trace", "debug", "info", "warning", "error", "critical", "off" };
        TCLAP::ValuesConstraint<std::string> allowedVals( allowed );
        TCLAP::ValueArg<std::string> loggingLevel("v", "verbose",
            "Logging output type (info, debug, critical)", false, "info",
            &allowedVals);
        TCLAP::ValueArg<unsigned> numThreads("t", "threads",
            "Number of threads to run processing work", false, 0, "int");
        TCLAP::ValueArg<std::string> waitRange("r", "wait-range",
            "Waiting time interval in seconds (e.g. 1.34,3.56)", false, "0,0", "x,y");

        cmd.add(protocol);
        cmd.add(margoFile);
        cmd.add(clientFile);
        cmd.add(dataSetName);
        cmd.add(productLabel);
        cmd.add(productSizes);
        cmd.add(loggingLevel);
        cmd.add(numThreads);
        cmd.add(waitRange);

        cmd.parse(argc, argv);

        g_protocol        = protocol.getValue();
        g_margo_file      = margoFile.getValue();
        g_connection_file = check_file_exists(clientFile.getValue());
        g_input_dataset   = dataSetName.getValue();
        g_product_label   = productLabel.getValue();
        g_product_sizes   = parse_product_sizes(productSizes.getValue());
        g_logging_level   = spdlog::level::from_str(loggingLevel.getValue());
        g_num_threads     = numThreads.getValue();
        g_wait_range      = parse_wait_range(waitRange.getValue());

    } catch(TCLAP::ArgException &e) {
        if(g_rank == 0) {
            spdlog::critical("{} for command-line argument {}", e.error(), e.argId());
            MPI_Abort(MPI_COMM_WORLD, 1);
            exit(-1);
        }
    }
}

static std::pair<double,double> parse_wait_range(const std::string& s) {
    std::pair<double,double> range = { 0.0, 0.0 };
    std::regex rgx("^((0|([1-9][0-9]*))(\\.[0-9]+)?)(,((0|([1-9][0-9]*))(\\.[0-9]+)?))?$");
    // groups 1 and 6 will contain the two numbers
    std::smatch matches;

    if(std::regex_search(s, matches, rgx)) {
        range.first = atof(matches[1].str().c_str());
        if(matches[6].str().size() != 0) {
            range.second = atof(matches[6].str().c_str());
        } else {
            range.second = range.first;
        }
    } else {
        if(g_rank == 0) {
            spdlog::critical("Invalid wait range expression {} (should be \"x,y\" where x and y are floats)", s);
            MPI_Abort(MPI_COMM_WORLD, -1);
            exit(-1);
        }
    }
    if(range.second < range.first) {
        spdlog::critical("Invalid wait range expression {} ({} < {})",
                         s, range.second, range.first);
        MPI_Abort(MPI_COMM_WORLD, -1);
        exit(-1);
    }

    return range;
}

static void run_benchmark() {

    hepnos::DataStore datastore;
    try {
        spdlog::trace("Connecting to HEPnOS using file {}", g_connection_file);
        datastore = hepnos::DataStore::connect(g_protocol, g_connection_file, g_margo_file);
    } catch(const hepnos::Exception& ex) {
        spdlog::critical("Could not connect to HEPnOS service: {}", ex.what());
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    {
        spdlog::trace("Creating AsyncEngine with {} threads", g_num_threads);
        hepnos::AsyncEngine async(datastore, g_num_threads);

        hepnos::RunDescriptor run_descriptor;

        if(g_rank == 0) {
            spdlog::trace("Creating dataset");
            auto dataset = datastore.root().createDataSet(g_input_dataset);
            auto run = dataset.createRun(0);
            run.toDescriptor(run_descriptor);
        }
        MPI_Bcast(&run_descriptor, sizeof(run_descriptor), MPI_BYTE, 0, MPI_COMM_WORLD);
        auto run = hepnos::Run::fromDescriptor(datastore, run_descriptor, false);
        auto subrun = run.createSubRun(g_rank);

        // create dummy products
        std::vector<dummy_product> products;
        products.resize(g_product_sizes.size());
        for(size_t i = 0; i < products.size(); i++) {
            products[i].data.resize(g_product_sizes[i]);
            for(size_t j = 0; j < g_product_sizes[i]; j++)
                products[i].data[j] = j % 256;
        }

        MPI_Barrier(MPI_COMM_WORLD);

        hepnos::EventNumber evn = 0;
        for(const auto& product : products) {
            auto event = subrun.createEvent(evn);
            hepnos::StoreStatistics stats;
            event.store(g_product_label, product, &stats);
            spdlog::info("size={}, storage={}, serialization={}", product.data.size(),
                         stats.raw_storage_time.max, stats.serialization_time.max);
            evn += 1;
        }

        MPI_Barrier(MPI_COMM_WORLD);
        evn = 0;

        std::vector<dummy_product> loaded_products;
        for(const auto& product : products) {
            auto event = subrun[evn];
            dummy_product tmp_product;
            hepnos::LoadStatistics stats;
            event.load(g_product_label, tmp_product, &stats);
            if(tmp_product.data != product.data) {
                spdlog::error("Loaded product doesn't match stored product!");
            }
            spdlog::info("size={}, loading={}, deserialization={}", product.data.size(),
                         stats.raw_loading_time.max, stats.deserialization_time.max);
            evn += 1;
        }

    }

    MPI_Barrier(MPI_COMM_WORLD);
    if(g_rank == 0) {
        datastore.shutdown();
    }
}

static std::string check_file_exists(const std::string& filename) {
    spdlog::trace("Checking if file {} exists", filename);
    std::ifstream ifs(filename);
    if(ifs.good()) return filename;
    else {
        spdlog::critical("File {} does not exist", filename);
        MPI_Abort(MPI_COMM_WORLD, -1);
        exit(-1);
    }
    return "";
}

static std::vector<size_t> parse_product_sizes(const std::string& str) {
    std::stringstream ss(str);
    std::vector<size_t> result;
    for(size_t i; ss >> i;) {
        result.push_back(i);
        if(ss.peek() == ',')
            ss.ignore();
    }
    return result;
}
