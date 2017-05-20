//
// Created by harta on 3/4/17.
//

#include <boost/serialization/unordered_map.hpp>
#include <iostream>
#include <boost/archive/binary_iarchive.hpp>
#include <fstream>
#include <map>
#include "EntapExecute.h"
#include "ExceptionHandler.h"
#include "EntapConsts.h"
#include "EntapInit.h"
#include <thread>
#include <list>
#include "csv.h"
#include "QuerySequence.h"
#include "FrameSelection.h"
#include "ExpressionAnalysis.h"
#include "SimilaritySearch.h"
#include <string>
#include <boost/regex.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <queue>

namespace boostFS = boost::filesystem;

namespace entapExecute {
    ExecuteStates state;
    std::string _frame_selection_exe, _expression_exe, _diamond_exe, _outpath, _entap_outpath;
    bool _blastp = false; // false for blastx, true for blastp
    bool _isProtein;      // input sequence, might want to handle differently

    void execute_main(boost::program_options::variables_map &user_input, std::string exe_path,
                      std::unordered_map<std::string, std::string> &config_map) {
        entapInit::print_msg("enTAP Executing...");


        int threads = entapInit::get_supported_threads(user_input);
        std::list<std::string> diamond_out, databases;
        std::string input_path, rsem_out, genemark_out;

        bool is_paired = (bool) user_input.count("paired-end");
        input_path = user_input["input"].as<std::string>();        // Gradually changes between runs

        if (user_input.count(ENTAP_CONFIG::INPUT_FLAG_RUNNUCLEOTIDE)) _isProtein = false;
        if (user_input.count(ENTAP_CONFIG::INPUT_FLAG_RUNPROTEIN)) _isProtein = true;
        bool is_overwrite = (bool) user_input.count(ENTAP_CONFIG::INPUT_FLAG_OVERWRITE);

        boostFS::path working_dir(boostFS::current_path());
        _outpath = working_dir.string() + "/" + user_input["tag"].as<std::string>() + "/";
        _entap_outpath = _outpath + ENTAP_EXECUTE::ENTAP_OUTPUT;
        boostFS::create_directories(_entap_outpath);
        std::string log_path = _outpath + ENTAP_CONFIG::LOG_FILENAME;
        remove(log_path.c_str());

        // init_databases
        std::vector<std::string> other_databases, contaminants;
        if (user_input.count("database")) {
            other_databases = user_input["database"].as<std::vector<std::string>>();
        } else other_databases.push_back(ENTAP_CONFIG::NCBI_NULL);

        // init_contam_filter
        if (user_input.count("contam")) {
            contaminants = user_input["contam"].as<std::vector<std::string>>();
        } else contaminants.push_back("");
        for (int ind = 0; ind < contaminants.size(); ind++) {
            if (contaminants[ind].empty()) continue;
            std::string &str = contaminants[ind];
            std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        }

        // init_state_control
        std::string user_state_str;
        bool state_flag = false;
        std::queue<char> state_queue;
        if (user_input.count("state")) {
            user_state_str = user_input["state"].as<std::string>();
            for (char c : user_state_str) {
                state_queue.push(c);
            }
        }

        state = INIT;
        try {
            databases = verify_databases(user_input["uniprot"].as<std::vector<std::string>>(),
                                         user_input["ncbi"].as<std::vector<std::string>>(), other_databases, exe_path,
                                         config_map);
            init_exe_paths(config_map, exe_path);
        } catch (ExceptionHandler &e) { throw e; }
        verify_state(state_queue, state_flag);

        FrameSelection genemark = FrameSelection(input_path, _frame_selection_exe, _outpath, is_overwrite);
        ExpressionAnalysis rsem = ExpressionAnalysis(input_path, threads, _expression_exe, _outpath, is_overwrite);
        SimilaritySearch diamond = SimilaritySearch(databases, input_path, threads, is_overwrite, _diamond_exe,
                                                    _outpath);
        std::map<std::string, QuerySequence> SEQUENCE_MAP = init_sequence_map(input_path);

        while (state != EXIT) {
            try {
                switch (state) {
                    case FRAME_SELECTION:
                        if (_isProtein) {
                            entapInit::print_msg("Protein sequences input, skipping frame selection");
                            genemark_out = input_path;
                            _blastp = true;
                            break;
                        }
                        genemark_out = genemark.execute(0,SEQUENCE_MAP);
                        input_path = genemark_out;
                        _blastp = true;
                        break;
                    case RSEM:
                        if (!user_input.count(ENTAP_CONFIG::INPUT_FLAG_ALIGN)) {
                            throw ExceptionHandler("No alignment file specified",
                                                   ENTAP_ERR::E_INPUT_PARSE);
                        }
                        rsem.execute(0, is_paired, user_input[ENTAP_CONFIG::INPUT_FLAG_ALIGN].as<std::string>());
                        break;
                    case FILTER:
                        input_path = filter_transcriptome(genemark_out, rsem_out, user_input["fpkm"].as<float>(),
                                                          input_path,
                                                          is_overwrite);
                        break;
                    case DIAMOND_RUN:
                        diamond_out = diamond.execute(0, input_path, _blastp);
                        break;
                    case DIAMOND_PARSE:
                        diamond_parse(diamond_out, contaminants, user_input["e"].as<double>(), input_path, exe_path);
                    default:
                        state = EXIT;
                        break;
                }
                verify_state(state_queue, state_flag);
            } catch (ExceptionHandler &e) {
                throw e;
            }
        }
    }

    std::list<std::string> verify_databases(std::vector<std::string> uniprot, std::vector<std::string> ncbi,
                                            std::vector<std::string> database, std::string &exe,
                                            std::unordered_map<std::string, std::string> &config) {
        entapInit::print_msg("Verifying databases...");
        // return file paths
        // config file paths already exist (checked in main)
        std::list<std::string> file_paths;

        std::string path, config_path;
        entapInit::print_msg("Verifying uniprot databases...");
        if (uniprot.size() > 0) {
            for (auto const &u_flag:uniprot) {
                if (u_flag.compare(ENTAP_CONFIG::INPUT_UNIPROT_NULL) != 0) {
                    if (u_flag == ENTAP_CONFIG::INPUT_UNIPROT_SWISS) {
                        config_path = config.at(ENTAP_CONFIG::KEY_UNIPROT_SWISS);
                    } else if (u_flag == ENTAP_CONFIG::INPUT_UNIPROT_TREMBL) {
                        config_path = config.at(ENTAP_CONFIG::KEY_UNIPROT_TREMBL);
                    } else if (u_flag == ENTAP_CONFIG::INPUT_UNIPROT_UR90) {
                        config_path = config.at(ENTAP_CONFIG::KEY_UNIPROT_UR90);
                    } else if (u_flag == ENTAP_CONFIG::INPUT_UNIPROT_UR100) {
                        config_path = config.at(ENTAP_CONFIG::KEY_UNIPROT_UR100);
                    }
                    if (!config_path.empty()) {
                        entapInit::print_msg("Config file database found, using this path at: " +
                                             config_path);
                        path = config_path;
                    } else {
                        path = exe + ENTAP_CONFIG::UNIPROT_INDEX_PATH + u_flag + ".dmnd";
                    }
                    if (!entapInit::file_exists(path))
                        throw ExceptionHandler("Database located at: " + path + " not found", ENTAP_ERR::E_INPUT_PARSE);
                    file_paths.push_back(path);
                } else {
                    entapInit::print_msg("No/null Uniprot databases detected");
                    break;
                }
            }
        }
        entapInit::print_msg("Complete");
        entapInit::print_msg("Verifying NCBI databases...");
        if (ncbi.size() > 0) {
            for (auto const &u_flag:ncbi) {
                if (u_flag.compare(ENTAP_CONFIG::NCBI_NULL) != 0) {
                    if (u_flag == ENTAP_CONFIG::NCBI_NONREDUNDANT) {
                        config_path = config.at(ENTAP_CONFIG::KEY_NCBI_NR);
                    } else if (u_flag == ENTAP_CONFIG::NCBI_REFSEQ_PLANT) {
                        config_path = config.at(ENTAP_CONFIG::KEY_NCBI_REFSEQ_SEPARATE);
                    } else if (u_flag == ENTAP_CONFIG::NCBI_REFSEQ_COMP) {
                        config_path = config.at(ENTAP_CONFIG::KEY_NCBI_REFSEQ_COMPLETE);
                    }
                    if (!config_path.empty()) {
                        entapInit::print_msg("Config file database found, using this path at: " +
                                             config_path);
                        path = config_path;
                    } else {
                        path = exe + ENTAP_CONFIG::NCBI_INDEX_PATH + u_flag + ".dmnd";
                    }
                    if (!entapInit::file_exists(path))
                        throw ExceptionHandler("Database located at: " + path + " not found", ENTAP_ERR::E_INPUT_PARSE);
                    file_paths.push_back(path);
                } else {
                    entapInit::print_msg("No/null NCBI databases detected");
                    break;
                }
            }
        }
        entapInit::print_msg("Complete");
        entapInit::print_msg("Verifying other databases...");
        if (database.size() > 0) {
            for (auto const &data_path:database) {
                if (data_path.compare(ENTAP_CONFIG::NCBI_NULL) == 0) continue;
                if (!entapInit::file_exists(data_path)) {
                    throw ExceptionHandler("Database located at: " + data_path + " not found",
                                           ENTAP_ERR::E_INPUT_PARSE);
                }
                boostFS::path bpath(data_path);
                std::string ext = bpath.extension().string();
                if (ext.compare(".dmnd") == 0) {
                    entapInit::print_msg("User has input a diamond indexed database at: " +
                                         data_path);
                    file_paths.push_back(data_path);
                    continue;
                } else {
                    //todo fix
                    entapInit::print_msg("User has input a database at: " + data_path);
                    std::string test_path = exe + ENTAP_CONFIG::BIN_PATH + data_path + ".dmnd";
                    entapInit::print_msg("Checking if indexed file exists at: " + test_path);
                    if (!entapInit::file_exists(test_path)) {
                        throw ExceptionHandler("Database located at: " + data_path + " not found",
                                               ENTAP_ERR::E_INPUT_PARSE);
                    } else {
                        file_paths.push_back(test_path);
                    }
                }
            }
        }
        entapInit::print_msg("Verification complete!");
        if (file_paths.size() > 0) {
            std::string database_final = "\n\nDatabases selected:\n";
            for (std::string base: file_paths) {
                database_final += base + "\n";
            }
            entapInit::print_msg(database_final);
        } else {
//            throw ExceptionHandler("No databases selected, exiting...",
//                ENTAP_ERR::E_RUN_VERIFY_DATABASES);
            entapInit::print_msg("No databases selected, some funcionality "
                                         "may not be able to run");
        }
        return file_paths;
    }


    std::string filter_transcriptome(std::string &genemark_path, std::string &rsem_path,
                                     float fpkm, std::string input_path, bool overwrite) {
        entapInit::print_msg("Beginning to filter transcriptome...");
        bool genemark, rsem;

        boostFS::path file_name(input_path);
        file_name.filename();
        std::string out_path = _entap_outpath +
                               file_name.stem().stem().string() + "_final.fasta";
        std::string out_removed = _entap_outpath +
                                  file_name.stem().string() + "_filtered_out.fasta";
        if (overwrite) {
            entapInit::print_msg("Overwrite selected, removing files:\n" +
                                 out_path + "\n" + out_removed);
            try {
                remove(out_path.c_str());
                remove(out_removed.c_str());
            } catch (std::exception const &e) {
                throw ExceptionHandler(e.what(), ENTAP_ERR::E_RUN_FILTER);
            }
        } else {
            entapInit::print_msg("Overwrite unselected, finding files..");
            if (!entapInit::file_exists(out_path)) {
                entapInit::print_msg("File not found at: " + out_path + " continue filtering");
            } else {
                entapInit::print_msg("File found at: " + out_path + " not filtering");
                return out_path;
            }
        }
        if (genemark_path.empty()) {
            entapInit::print_msg("Looking for genemark file");
            std::string temp_path = ENTAP_EXECUTE::GENEMARK_EXE_PATH + "genemark" + "/" +
                                    file_name.string() + ".faa";
            std::cout << temp_path << std::endl;
            if (entapInit::file_exists(temp_path)) {
                entapInit::print_msg("File found at: " + temp_path);
                genemark_path = temp_path;
                genemark = true;
            } else {
                entapInit::print_msg("File was not found.");
                genemark = false;
            }
        } else genemark = true;
        if (rsem_path.empty()) {
            entapInit::print_msg("Looking for rsem file");
            std::string temp_path = _outpath + "rsem" + "/" +
                                    file_name.stem().string() + ".genes.results";
            if (entapInit::file_exists(temp_path)) {
                entapInit::print_msg("File found at: " + temp_path);
                rsem_path = temp_path;
                rsem = true;
            } else {
                entapInit::print_msg("File was not found.");
                rsem = false;
            }
        } else rsem = true;

        if (!rsem && !genemark) {
            entapInit::print_msg("Neither genemark, nor rsem files were found, no filtering will be done.");
            return input_path;
        }

        if (genemark && !rsem) {
            entapInit::print_msg("No rsem file found, so genemark results will continue as main trancriptome: " +
                                 genemark_path);
            boostFS::copy_file(genemark_path, out_path);
            return out_path;
        }

        std::string process_file;
        !genemark ? process_file = input_path : process_file = genemark_path;
        entapInit::print_msg("Filtering file located at: " + process_file);

        io::CSVReader<ENTAP_EXECUTE::RSEM_COL_NUM, io::trim_chars<' '>,
                io::no_quote_escape<'\t'>> in(rsem_path);
        std::unordered_map<std::string, float> expression_map;
        in.next_line();
        std::string geneid, transid;
        float length, e_leng, e_count, tpm, fpkm_val;
        while (in.read_row(geneid, transid, length, e_leng, e_count, tpm, fpkm_val)) {
            expression_map.emplace(geneid, fpkm_val);
        }
        std::ifstream in_file(process_file);
        std::ofstream out_file(out_path, std::ios::out | std::ios::app);
        std::ofstream removed_file(out_removed, std::ios::out | std::ios::app);
        boost::smatch match;
        bool filtered = false;
        double removed_count = 0;
        for (std::string line; getline(in_file, line);) {
            boost::regex exp(">(\\S+)", boost::regex::icase);
            if (boost::regex_search(line, match, exp)) {
                std::string id = std::string(match[1].first, match[1].second);
                if (expression_map.find(id) != expression_map.end()) {
                    float fp = expression_map.at(id);
                    if (fp > fpkm) {
                        filtered = true;
                        out_file << line;
                    } else {
                        filtered = false;
                        removed_count++;
                        removed_file << line << std::endl;
                    }
                } else {
                    // default if not found is NOT remove it
                    filtered = true;
                    out_file << line << std::endl;
                }
            } else {
                // anything not a seq header
                if (filtered) {
                    out_file << line << std::endl;
                } else removed_file << line << std::endl;
            }
        }
        in_file.close();
        out_file.close();
        removed_file.close();
        entapInit::print_msg("File successfully filtered. Outputs at: " + out_path + " and: " +
                             out_removed);
        return out_path;
    }


    // input: 3 database string array of selected databases
    void diamond_parse(std::list<std::string> diamond_files, std::vector<std::string> contams,
                       double user_e, std::string transcriptome, std::string &exe) {
        entapInit::print_msg("Beginning to filter individual diamond_files...");
        std::unordered_map<std::string, std::string> taxonomic_database;
        std::map<std::string, QuerySequence> query_map;
        try {
            taxonomic_database = read_tax_map(exe);
        } catch (ExceptionHandler &e) {
            throw ExceptionHandler(e.what(), e.getErr_code());
        }
        if (diamond_files.empty()) diamond_files = find_diamond_files();
        if (diamond_files.empty()) throw ExceptionHandler("No diamond files found", ENTAP_ERR::E_INPUT_PARSE);
        boostFS::path input_file(transcriptome);
        input_file = input_file.stem();
        if (input_file.has_stem()) input_file = input_file.stem();
        std::string out_contaminants = _entap_outpath + input_file.string() + "_contaminants.tsv";
        std::string out_filtered = _entap_outpath + input_file.string() + "_filtered.tsv";
        // both contaminants and bad hits
        std::string out_removed = _entap_outpath + input_file.string() + "_removed.tsv";
        print_header(out_removed);
        print_header(out_contaminants);
        std::ofstream file_contaminants(out_contaminants, std::ios::out | std::ios::app);
        std::ofstream file_removed(out_removed, std::ios::out | std::ios::app);

        for (std::string data : diamond_files) {
            entapInit::print_msg("Diamond file located at " + data + " being filtered");
            io::CSVReader<ENTAP_EXECUTE::diamond_col_num, io::trim_chars<' '>, io::no_quote_escape<'\t'>> in(data);
            // todo have columns from input file, in_read_header for versatility
//            in.read_header(io::ignore_extra_column,"qseqid", "sseqid", "pident", "length", "mismatch", "gapopen",
//            "qstart", "qend", "sstart", "send", "evalue", "bitscore", "stitle");
            std::string qseqid, sseqid, stitle;
            float evalue, pident, bitscore;
            int length, mismatch, gapopen, qstart, qend, sstart, send;
            while (in.read_row(qseqid, sseqid, pident, length, mismatch, gapopen,
                               qstart, qend, sstart, send, evalue, bitscore, stitle)) {
                QuerySequence new_query = QuerySequence(data, qseqid, sseqid, pident, length, mismatch, gapopen,
                                                        qstart, qend, sstart, send, evalue, bitscore, stitle, user_e);
                boost::regex exp("\\[([^]]+)\\](?!.+\\[.+\\])");      // TODO determined by database format
                boost::smatch match;
                if (boost::regex_search(stitle, match, exp)) {
                    std::string species_lower = std::string(match[1].first, match[1].second);
                    std::string species = species_lower;
                    std::transform(species_lower.begin(), species_lower.end(), species_lower.begin(), ::tolower);
                    if (taxonomic_database.find(species_lower) != taxonomic_database.end()) {
                        new_query.setSpecies(species);
                        new_query.setContaminant(is_contaminant(species_lower, taxonomic_database, contams));
                    } else {
                        new_query.setSpecies(species);
                        new_query.setContaminant(false);
                    }
                } else {
                    new_query.setSpecies("NOT_FOUND");
                    new_query.setContaminant(false);
                }
                //boost::regex exp_informative("")     // TODO use regex(database specific)
                try {
                    unsigned long a = stitle.find_last_of('|') + 2;
                    unsigned long b = stitle.find_first_of('[');
                    std::string informative = stitle.substr(a, b - a);
                    new_query.setInformative(informative);
                } catch (...) {
                    new_query.setInformative(stitle);
                }
                if (new_query.isContaminant()) {
                    file_contaminants << new_query << std::endl;
                    continue;
                }
                // can implement buckets if memory is not issue
                std::map<std::string, QuerySequence>::iterator it = query_map.find(qseqid);
                if (it != query_map.end()) {
//                    QuerySequence temp = query_map.at(qseqid);
                    // todo filter database files separately
                    if (new_query > it->second) {
                        if (it->second.isContaminant()) {
                            file_contaminants << it->second << std::endl;
                        }
                        file_removed << it->second << std::endl;
                        it->second = new_query;
                    } else {
                        file_removed << it->second << std::endl;
                    }
                } else {
                    query_map.emplace(qseqid, new_query);
                }
            }
        }
        file_contaminants.close();
        file_removed.close();
        print_filtered_map(query_map, out_filtered);
    }


    std::unordered_map<std::string, std::string> read_tax_map(std::string &exe) {
        entapInit::print_msg("Reading taxonomic database into memory...");
        std::unordered_map<std::string, std::string> restored_map;
        try {
            {
                std::ifstream ifs(exe + ENTAP_CONFIG::TAX_BIN_PATH);
                boost::archive::binary_iarchive ia(ifs);
                ia >> restored_map;
            }
        } catch (std::exception &exception) {
            throw ExceptionHandler(exception.what(), ENTAP_ERR::E_INIT_TAX_READ);
        }
        entapInit::print_msg("Success!");
        return restored_map;
    }

    std::map<std::string, QuerySequence> init_sequence_map(std::string &input_file) {
        std::map<std::string, QuerySequence> seq_map;
        std::ifstream in_file(input_file);
        std::string line, sequence, seq_id;
        while (getline(in_file, line)) {
            if (line.empty()) continue;
            if (line.find(">") == 0) {
                if (!seq_id.empty()) {
                    seq_map.emplace(seq_id, QuerySequence(_blastp, sequence));
                }
                unsigned long first = line.find(">")+1;
                unsigned long second = line.find(" ");
                seq_id = line.substr(first, second-first);
                sequence = line + "\n";
            } else {
                sequence += line + "\n";
            }
        }
        seq_map.emplace(seq_id, QuerySequence(_blastp, sequence));
        in_file.close();
        return seq_map;

    }

    bool is_contaminant(std::string species, std::unordered_map<std::string, std::string> &database,
                        std::vector<std::string> &contams) {
        // species and tax database both lowercase
        // already checked if in database
        std::string id_lineage = database[species];
        std::transform(id_lineage.begin(), id_lineage.end(), id_lineage.begin(), ::tolower);
        for (auto const &contaminant:contams) {
            if (id_lineage.find(contaminant) != std::string::npos) return true;
        }
        return false;
    }

    std::list<std::string> find_diamond_files() {
        entapInit::print_msg("Diamond files were not inputted, searching in : " + ENTAP_CONFIG::DIAMOND_DIR);
        std::list<std::string> out_list;
        boostFS::path p(ENTAP_CONFIG::DIAMOND_DIR);
        for (auto &file : boost::make_iterator_range(boostFS::directory_iterator(p), {})) {
            if (file.path().string().find("_std") != std::string::npos) {
                continue;
            }
            out_list.push_back(file.path().string());
            entapInit::print_msg("File found at: " + file.path().string());
        }
        entapInit::print_msg("Done searching for files");
        return out_list;
    }

    void print_filtered_map(std::map<std::string, QuerySequence> &map, std::string &out) {
        print_header(out);
        std::ofstream file_filtered(out, std::ios::out | std::ios::app);
        for (std::map<std::string, QuerySequence>::iterator it = map.begin(); it != map.end(); ++it) {
            file_filtered << it->second << std::endl;
        }
        file_filtered.close();
    }

    void print_header(std::string file) {
        std::ofstream ofstream(file, std::ios::out | std::ios::app);
        ofstream << "qseqid" << '\t' << "seqid" << '\t' << "pident" << '\t' <<
                 "length" << '\t' << "mismatch" << '\t' << "mismatch" << '\t' <<
                 "gapopen" << '\t' << "qstart" << '\t' << "qend" << '\t' <<
                 "sstart" << '\t' << "send" << '\t' << "e_val" << '\t' <<
                 "informative" << '\t' << "species" << '\t' << "database_path" << std::endl;
        ofstream.close();
    }

    void print_statistics(std::string &msg, std::string &out_path) {
        std::string file_path = out_path + ENTAP_CONFIG::LOG_FILENAME;
        std::ofstream log_file(file_path, std::ios::out | std::ios::app);
        log_file << msg << std::endl;
        log_file.close();
    }

    // Doesn't check default paths if user does not want to use that portion of enTAP
    // returns diamond path for config
    std::string init_exe_paths(std::unordered_map<std::string, std::string> &map, std::string &exe) {
        entapInit::print_msg("Verifying execution paths...");
        std::string temp_rsem = map[ENTAP_CONFIG::KEY_RSEM_EXE];
        std::string temp_diamond = map[ENTAP_CONFIG::KEY_DIAMOND_EXE];
        std::string temp_genemark = map[ENTAP_CONFIG::KEY_GENEMARK_EXE];

        if (temp_rsem.empty()) {
            entapInit::print_msg("RSEM config path empty, setting to default: " +
                                 exe + ENTAP_EXECUTE::RSEM_EXE_PATH);
            temp_rsem = exe + ENTAP_EXECUTE::RSEM_EXE_PATH;
        } else {
            entapInit::print_msg("RSEM path set to: " + temp_rsem);
        }
        if (temp_diamond.empty()) {
            entapInit::print_msg("DIAMOND config path empty, setting to default: " +
                                 exe + ENTAP_CONFIG::DIAMOND_PATH_EXE);
            temp_diamond = exe + ENTAP_CONFIG::DIAMOND_PATH_EXE;
        } else {
            entapInit::print_msg("DIAMOND path set to: " + temp_diamond);
        }
        if (temp_genemark.empty()) {
            entapInit::print_msg("GenemarkS-T config path empty, setting to default: " +
                                 exe + ENTAP_EXECUTE::GENEMARK_EXE_PATH);
            temp_genemark = exe + ENTAP_EXECUTE::GENEMARK_EXE_PATH;
        } else {
            entapInit::print_msg("RSEM path set to: " + temp_genemark);
        }
        _diamond_exe = temp_diamond;
        _frame_selection_exe = temp_genemark;
        _expression_exe = temp_rsem;
        return _diamond_exe;
    }

    //only assuming between 0-9 NO 2 DIGIT STATES
    void verify_state(std::queue<char> &queue, bool &test) {
        if (queue.empty()) {
            state = static_cast<ExecuteStates>(state + 1);
            if (!valid_state(state)) state = EXIT;
            return;
        }
        char first = queue.front();
        if (first == 'x') {
            queue.pop();
            test = false;
            if (queue.empty()) {
                state = EXIT;
                return;
            }
            verify_state(queue, test);
            return;
        }
        if (first == '+') {
            // assuming proper cast, state has been evaluated before
            test = true;
            queue.pop();
            char second = queue.front(); // assuming number
            if (!second) {
                // end of queue, might handle differently
                state = static_cast<ExecuteStates>(state + 1);
                if (!valid_state(state)) state = EXIT;
                return;
            }
            verify_state(queue, test);
        } else {
            // some number (assuming it was parsed before)
            int i = first - '0';
            if (state < i && test) {
                state = static_cast<ExecuteStates>(state + 1);
                return;
            } else if (state < i && !test) {
                state = static_cast<ExecuteStates>(i);
                return;
            }
            if (state == i) {
                queue.pop();
                test = false;
                verify_state(queue, test);
            }
        }
    }

    bool is_file_empty(std::string path) {
        std::ifstream ifstream(path);
        return ifstream.peek() == std::ifstream::traits_type::eof();
    }

    bool valid_state(ExecuteStates s) {
        return (s >= FRAME_SELECTION && s <= EXIT);
    }
}
