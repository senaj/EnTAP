/*
 *
 * Developed by Alexander Hart
 * Plant Computational Genomics Lab
 * University of Connecticut
 *
 * For information, contact Alexander Hart at:
 *     entap.dev@gmail.com
 *
 * Copyright 2017-2019, Alexander Hart, Dr. Jill Wegrzyn
 *
 * This file is part of EnTAP.
 *
 * EnTAP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EnTAP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EnTAP.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef ENTAP_QUERYSEQUENCE_H
#define ENTAP_QUERYSEQUENCE_H

#include "common.h"
#include "EntapExecute.h"
#include "database/EntapDatabase.h"

class QueryAlignment;

class QuerySequence {
public:

    typedef std::vector<QueryAlignment*> align_database_hits_t;
    typedef std::unordered_map<std::string,align_database_hits_t> ALIGNMENT_DATA_T;

    typedef enum {

        QUERY_BLAST_HIT         = (1 << 0),
        QUERY_EGGNOG_HIT        = (1 << 1),
        QUERY_EXPRESSION_KEPT   = (1 << 2),
        QUERY_FRAME_KEPT        = (1 << 3),
        QUERY_FAMILY_ASSIGNED   = (1 << 4),
        QUERY_ONE_KEGG          = (1 << 5),
        QUERY_ONE_GO            = (1 << 6),
        QUERY_INFORMATIVE       = (1 << 7),
        QUERY_INTERPRO          = (1 << 8),
        QUERY_IS_PROTEIN        = (1 << 9),
        QUERY_BLASTED           = (1 << 10),
        QUERY_CONTAMINANT       = (1 << 11),
        QUERY_FAMILY_ONE_KEGG   = (1 << 12),
        QUERY_FAMILY_ONE_GO     = (1 << 13),
        QUERY_ONT_INTERPRO_GO   = (1 << 14),
        QUERY_ONT_INTERPRO_PATHWAY = (1 << 15),

        QUERY_MAX               = (1 << 31)

    } QUERY_FLAGS;

    struct EggnogResults {
        std::string              member_ogs;        // 0A01R@biNOG,0V8CP@meNOG (ortholgous groups)
        std::string              seed_ortholog;     // 34740.HMEL017225-PA
        std::string              seed_evalue;       // Pulled from DIAMOND run
        std::string              seed_score;        // Pulled from DIAMOND run
        std::string              seed_coverage;     // Pulled from DIAMOND run
        std::string              predicted_gene;    // Most common predicted gene (pname)
        std::string              tax_scope_lvl_max; // virNOG[6]
        std::string              tax_scope;         // virNOG
        std::string              tax_scope_readable;// Ascomycota
        std::string              pname;             // All predicted gene names
        std::string              name;
        std::string              bigg;
        std::string              kegg;
        std::string              og_key;            // Used for indexing into older SQL database (if using)
        std::string              description;       // Used for older version
        std::string              protein_domains;
        fp64                     seed_eval_raw;     // Used for finding best hit
        go_format_t              parsed_go;         // All go terms found
    };

    struct InterProResults {
        std::string             e_value;
        std::string             database_desc_id;
        std::string             database_type;
        std::string             interpro_desc_id;
        std::string             pathways;
        fp64                    e_value_raw;
        go_format_t             parsed_go;
    };

    struct SimSearchResults {
        std::string                       length;
        std::string                       mismatch;
        std::string                       gapopen;
        std::string                       qstart;
        std::string                       qend;
        std::string                       sstart;
        std::string                       send;
        std::string                       pident;
        std::string                       bit_score;
        std::string                       e_val;
        std::string                       coverage;
        std::string                       database_path;
        std::string                       qseqid;
        std::string                       sseqid;
        std::string                       stitle;
        std::string                       species;
        std::string                       contam_type;
        std::string                       lineage;
        std::string                       yes_no_contam; // just for convenience
        std::string                       yes_no_inform;
        fp32                              tax_score;     // taxonomic score, may be based on parent
        fp64                              e_val_raw;
        fp64                              coverage_raw;
        bool                              contaminant;
        bool                              is_informative;
        UniprotEntry                      uniprot_info;
    };




    struct AlignmentData {
        ALIGNMENT_DATA_T sim_search_data[SIM_SOFTWARE_COUNT];
        ALIGNMENT_DATA_T ontology_data[ONT_SOFTWARE_COUNT];
        QuerySequence* querySequence;

        QueryAlignment* overall_alignment[EXECUTION_MAX][ONT_SOFTWARE_COUNT]{};

        struct sort_descending_database {
            bool operator () (QueryAlignment* first, QueryAlignment* second);
        };

        AlignmentData(QuerySequence* sequence);
        ~AlignmentData();

        void set_best_alignment(ExecuteStates state, uint16 software, QueryAlignment *);
        void update_best_hit(ExecuteStates state, uint16 software, std::string &database, QueryAlignment* new_alignment);
        bool hit_database(ExecuteStates state, uint16 software, std::string &database);
        align_database_hits_t* get_database_ptr(ExecuteStates, uint16, std::string&);
        QueryAlignment* get_best_align_ptr(ExecuteStates, uint16 software, std::string database);
        ALIGNMENT_DATA_T* get_software_ptr(ExecuteStates state, uint16 software);

    };



    /* Public Functions */
    QuerySequence();
    QuerySequence(bool, std::string, std::string);
    ~QuerySequence();
    std::string print_delim(std::vector<ENTAP_HEADERS> &, short lvl ,char delim);
    void setFrame(const std::string &frame);
    unsigned long getSeq_length() const;
    const std::string &getFrame() const;
    const std::string &get_sequence_p() const;
    void set_sequence_p(std::string &seq);
    const std::string &get_sequence_n() const;
    void set_sequence_n(const std::string &_sequence_n);
    const std::string &get_sequence() const;
    void set_fpkm(float _fpkm);
    bool is_kept();
    bool QUERY_FLAG_GET(QUERY_FLAGS flag);
    void QUERY_FLAG_SET(QUERY_FLAGS flag);
    void QUERY_FLAG_CLEAR(QUERY_FLAGS flag);
    void QUERY_FLAG_CHANGE(QUERY_FLAGS flag, bool val);
    bool isContaminant();
#ifdef EGGNOG_MAPPER
    void set_eggnog_results(const EggnogResults&);
#endif
    // Alignemnt accession routines
    void add_alignment(ExecuteStates state, uint16 software, EggnogResults &results, std::string& database);
    void add_alignment(ExecuteStates state, uint16 software, SimSearchResults &results, std::string& database,std::string lineage);
    void add_alignment(ExecuteStates state, uint16 software, InterProResults &results, std::string& database);
    QuerySequence::align_database_hits_t* get_database_hits(std::string& database,ExecuteStates state, uint16 software);

    std::string format_go_info(std::vector<std::string> &go_list, uint8 lvl);

    // Returns recast alignment pointer
    template<class T>
    T *get_best_hit_alignment(ExecuteStates state, uint16 software, std::string database) {
        return static_cast<T*>(_alignment_data->get_best_align_ptr(state, software, database));
    }

    // Checks whether an alignment was found against specific atabase
    bool hit_database(ExecuteStates state, uint16 software, std::string database);
    void update_query_flags(ExecuteStates state, uint16 software);
    void get_header_data(std::string& data, ENTAP_HEADERS header, uint8 lvl);
    void set_header_data();

private:
    fp32                              _fpkm;
    uint32                            _query_flags;
    std::string                       _seq_id;
    unsigned long                     _seq_length;
    std::string                       _sequence_p;
    std::string                       _sequence_n;
    std::string                       _frame;
    EggnogResults                     _eggnog_results;
    AlignmentData                     *_alignment_data;  // contains all alignment data
    std::string                       _header_info[ENTAP_HEADER_COUNT];

    /* Private Functions */
    void init_sequence();
    unsigned long calc_seq_length(std::string &,bool);

};


#endif //ENTAP_QUERYSEQUENCE_H
