/* ************************************************************************
 * Copyright 2018 Advanced Micro Devices, Inc.
 * ************************************************************************ */

#include "utility.hpp"
#include "rocsparse.hpp"
#include "testing_axpyi.hpp"

#include <iostream>
#include <stdio.h>
#include <boost/program_options.hpp>
#include <rocsparse.h>

namespace po = boost::program_options;

int main(int argc, char *argv[])
{
    Arguments argus;
    argus.unit_check = 0;
    argus.timing = 1;

    std::string function;
    char precision;

    rocsparse_int device_id;

    po::options_description desc("rocsparse client command line options");
    desc.add_options()("help,h", "produces this help message")
        ("sizen,n",
         po::value<rocsparse_int>(&argus.N)->default_value(128),
         "Specific vector size testing, LEVEL-1: the length of the dense vector.")

        ("sizennz,z",
         po::value<rocsparse_int>(&argus.nnz)->default_value(32),
         "Specific vector size testing, LEVEL-1: the number of non-zero elements "
         "of the sparse vector.")

        ("alpha", 
          po::value<double>(&argus.alpha)->default_value(1.0), "specifies the scalar alpha")
        
        ("function,f",
         po::value<std::string>(&function)->default_value("axpyi"),
         "SPARSE function to test. Options: axpyi")
        
        ("precision,r",
         po::value<char>(&precision)->default_value('s'), "Options: s,d")
        
        ("verify,v",
         po::value<rocsparse_int>(&argus.norm_check)->default_value(0),
         "Validate GPU results with CPU? 0 = No, 1 = Yes (default: No)")

        ("iters,i",
         po::value<rocsparse_int>(&argus.iters)->default_value(10),
         "Iterations to run inside timing loop")
        
        ("device",
         po::value<rocsparse_int>(&device_id)->default_value(0),
         "Set default device to be used for subsequent program runs");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    if (precision != 's' && precision != 'd')
    {
        fprintf(stderr, "Invalid value for --precision\n");
        return -1;
    }

    if (argus.nnz >= argus.N)
    {
        fprintf(stderr, "Number of non-zeros should be less than vector length\n");
        return -1;
    }

    // Device Query
    rocsparse_int device_count = query_device_property();

    if(device_count <= device_id)
    {
        fprintf(stderr, "Error: invalid device ID. There may not be such device ID. Will exit\n");
        return -1;
    }
    else
    {
        set_device(device_id);
    }

    /* ============================================================================================
     */
//    if(argus.M < 0 || argus.N < 0 || argus.K < 0)
//    {
//        fprintf(stderr, "Invalid matrix dimension\n");
//    }
    if (argus.N < 0)
    {
        fprintf(stderr, "Invalid dimension\n");
        return -1;
    }

    if(function == "axpyi")
    {
        if(precision == 's')
            testing_axpyi<rocsparse_int, float>(argus);
        else if(precision == 'd')
            testing_axpyi<rocsparse_int, double>(argus);
    }
    else
    {
        fprintf(stderr, "Invalid value for --function\n");
        return -1;
    }
    return 0;
}