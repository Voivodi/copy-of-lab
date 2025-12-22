add_executable(
    hamarc
    main.cpp

    ../lib/archiver.cpp
    ../lib/argparser.cpp
    ../lib/hamarc_core.cpp 
    ../lib/hamming_codec.cpp
    ../lib/parse_args.cpp
)

target_include_directories(hamarc PRIVATE
    ../lib
)

target_compile_features(hamarc PRIVATE cxx_std_20)
