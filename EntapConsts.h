//
// Created by harta55 on 2/1/17.
//

#ifndef ENTAP_ERRORFLAGS_H
#define ENTAP_ERRORFLAGS_H

namespace ENTAP_ERR {
    const int E_INPUT_PARSE = 10;
    const int E_SUCCESS = 11;
    const int E_INIT_TAX_DOWN = 20;
    const int E_INIT_TAX_INDEX = 21;
    const int E_INIT_TAX_SERIAL = 22;
    const int E_INIT_INDX_DATA_NOT_FOUND = 30;


    const int E_INIT_TAX_READ = 55;
}

namespace ENTAP_CONST {
    const std::string TAX_SCRIPT_PATH = "../download_tax.pl";
    const std::string TAX_DATABASE_PATH = "databases/ncbi_tax.entp";
    const std::string TAX_BIN_PATH = "bin/ncbi_tax_bin.entp";

    //------------------USER INPUTS-----------------------//
    const std::string INPUT_UNIPROT_SWISS = "swiss";
    const std::string INPUT_UNIPROT_UR100 = "ur100";
    const std::string INPUT_UNIPROT_UR90 = "ur90";
    const std::string INPUT_UNIPROT_TREMBL = "trembl";
    const std::string INPUT_UNIPROT_NULL = "null";
    const std::string INPUT_UNIPROT_DEFAULT = INPUT_UNIPROT_SWISS;

    const std::string UNIPROT_FTP_SWISS = "ftp://ftp.uniprot.org/pub/databases/"
            "uniprot/current_release/knowledgebase/complete/uniprot_sprot.fasta.gz";
    const std::string UNIPROT_BASE_PATH = "databases/uniprot_";

    const std::string NCBI_NONREDUNDANT = "nr";
    const std::string NCBI_REFSEQ = "refseq";
    const std::string NCBI_DEFAULT = NCBI_REFSEQ;

    const std::string DIAMOND_PATH_EXE = "../libs/diamond-0.8.31/bin/diamond";
    const std::string DIAMOND_INDX_OUT_PATH = "outfiles/diamond_index.out";
}



#endif //ENTAP_ERRORFLAGS_H
