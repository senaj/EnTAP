//
// Created by harta on 5/7/17.
//

#ifndef ENTAP_FRAMESELECTION_H
#define ENTAP_FRAMESELECTION_H
#include <iostream>


class FrameSelection {
    public:
        std::string execute(short);
        FrameSelection(std::string&, std::string&, std::string&, bool);
    private:
        std::string _exe_path,_inpath, _outpath;
        bool _overwrite;
        std::string genemarkst();
        std::string genemarkStats(std::string&);
};


#endif //ENTAP_FRAMESELECTION_H
