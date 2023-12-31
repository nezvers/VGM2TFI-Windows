#ifdef _WIN32
#define DIRECTORY_SEPARATOR "\\"
#else
#define DIRECTORY_SEPARATOR "/"
#endif

#define PATH_MAX 100

#include <stdio.h>
#include "unistd.h"
#include <limits.h>
#include <stdlib.h>
#include <cstring>
#include <zlib.h>
#include <filesystem>
namespace fs = std::filesystem;


struct fileListStruct {
    char* path;
    char* name;
    bool dir;
};



fileListStruct* fileList;
int fileListCnt;
int fileListSize;



struct operatorStruct {
    int multiple;//0..15
    int detune;//-3..0..3
    int totalLevel;//0..127
    int rateScale;//0..3
    int envAttack;//0..31
    int envDecay;//0..31
    int envSustain;//0..31
    int envRelease;//0..15
    int envRelLevel;//0..15
    int envType;//7..15
};



struct instrumentStruct {
    operatorStruct op[4];
    int algo;//0..7
    int feedback;//0..7
    int id;//to detect same instruments with different volume
    int fileId;
};



instrumentStruct insChn[6];
instrumentStruct insChnPrev[6];
instrumentStruct* insList;

int insListCnt;
int insListSize;

bool dacOn;



bool ins_save(instrumentStruct* ins, const char* filename)
{
    unsigned char insData[42];
    int aa, pp;
    FILE* file;

    pp = 0;
    insData[pp++] = ins->algo;
    insData[pp++] = ins->feedback;

    for (aa = 0; aa < 4; aa++)
    {
        insData[pp++] = ins->op[aa].multiple;
        insData[pp++] = ins->op[aa].detune + 3;
        insData[pp++] = ins->op[aa].totalLevel;
        insData[pp++] = ins->op[aa].rateScale;
        insData[pp++] = ins->op[aa].envAttack;
        insData[pp++] = ins->op[aa].envDecay;
        insData[pp++] = ins->op[aa].envSustain;
        insData[pp++] = ins->op[aa].envRelease;
        insData[pp++] = ins->op[aa].envRelLevel;
        insData[pp++] = ins->op[aa].envType;
    }

    file = fopen(filename, "wb");
    if (file)
    {
        fwrite(insData, pp, 1, file);
        fclose(file);
        return true;
    }
    return false;
}



void ins_add(instrumentStruct* ins, int fileId)
{
    if (insListCnt >= insListSize)
    {
        insListSize += 16;
        insList = (instrumentStruct*)realloc(insList, insListSize * sizeof(instrumentStruct));
    }
    memcpy(&insList[insListCnt], ins, sizeof(instrumentStruct));
    insList[insListCnt].fileId = fileId;
    insListCnt++;
}



int ins_find(instrumentStruct* ins)
{
    int i;

    if (!ins->op[0].envAttack && !ins->op[1].envAttack && !ins->op[2].envAttack && !ins->op[3].envAttack) return -1;
    if (ins->op[0].totalLevel > 0x7e && ins->op[1].totalLevel > 0x7e && ins->op[2].totalLevel > 0x7e && ins->op[3].totalLevel > 0x7e) return -1;

    for (i = 0; i < insListCnt; i++)
    {
        if (!memcmp(ins, &insList[i], sizeof(instrumentStruct))) return i;
    }

    return -1;
}



int ins_slot(instrumentStruct* ins)
{
    const int slotMap[8] = { 0x08,0x08,0x08,0x08,0x0c,0x0e,0x0e,0x0f };
    return slotMap[ins->algo & 7];
}



bool ins_compare_novol(instrumentStruct* ins1, instrumentStruct* ins2)
{
    operatorStruct* op1, * op2;
    int i, slot;

    if (ins1->algo != ins2->algo) return false;
    if (ins1->feedback != ins2->feedback) return false;

    slot = ins_slot(ins1);

    if (!(slot & 1)) if (ins1->op[0].totalLevel != ins2->op[0].totalLevel) return false;
    if (!(slot & 2)) if (ins1->op[1].totalLevel != ins2->op[1].totalLevel) return false;
    if (!(slot & 4)) if (ins1->op[2].totalLevel != ins2->op[2].totalLevel) return false;
    if (!(slot & 8)) if (ins1->op[3].totalLevel != ins2->op[3].totalLevel) return false;

    for (i = 0; i < 4; i++)
    {
        op1 = &ins1->op[i];
        op2 = &ins2->op[i];
        if (op1->envAttack != op2->envAttack) return false;
        if (op1->envDecay != op2->envDecay) return false;
        if (op1->envSustain != op2->envSustain) return false;
        if (op1->envRelease != op2->envRelease) return false;
        if (op1->envRelLevel != op2->envRelLevel) return false;
        if (op1->multiple != op2->multiple) return false;
        if (op1->detune != op2->detune) return false;
        if (op1->envType != op2->envType) return false;
        if (op1->rateScale != op2->rateScale) return false;
    }

    return true;
}



void write_fm(int bank, int reg, int val, int fileId)
{
    operatorStruct* op;
    int dtTable[8] = { 0,1,2,3,0,-1,-2,-3 };
    int ch;

    if (!bank)
    {
        switch (reg)
        {
        case 0x2b://DAC on/off
            dacOn = (val & 0x80) ? true : false;
            return;
        case 0x28://key on/off
            ch = val & 7;
            if (ch > 3) ch--;

            if (ch == 5 && dacOn) return;

            if (val & 0xf0)
            {
                if (memcmp(&insChnPrev[ch], &insChn[ch], sizeof(instrumentStruct)))
                {
                    if (ins_find(&insChn[ch]) < 0) ins_add(&insChn[ch], fileId);
                }
                memcpy(&insChnPrev[ch], &insChn[ch], sizeof(instrumentStruct));
            }
            return;
        }
    }

    if (reg < 0x30) return;
    if (reg > 0xb3) return;
    if ((reg & 3) == 3) return;

    ch = (reg & 3) + bank * 3;
    op = &insChn[ch].op[(reg >> 2) & 3];

    if (reg >= 0x30 && reg < 0x40)//DT1,MUL
    {
        op->detune = dtTable[(val >> 4) & 7];
        op->multiple = val & 0x0f;
        return;
    }
    if (reg >= 0x40 && reg < 0x50)//TL
    {
        op->totalLevel = val & 0x7f;
        return;
    }
    if (reg >= 0x50 && reg < 0x60)//RS,AR
    {
        op->rateScale = (val >> 6) & 3;
        op->envAttack = val & 0x1f;
        return;
    }
    if (reg >= 0x60 && reg < 0x70)//AM,D1R
    {
        op->envDecay = val & 0x1f;
        return;
    }
    if (reg >= 0x70 && reg < 0x80)//D2R
    {
        op->envSustain = val & 0x1f;
        return;
    }
    if (reg >= 0x80 && reg < 0x90)//D1L,RR
    {
        op->envRelLevel = val >> 4;
        op->envRelease = val & 0x0f;
        return;
    }
    if (reg >= 0x90 && reg < 0xa0)//SSG-EG
    {
        op->envType = val & 0x0f;
        return;
    }
    if (reg >= 0xb0 && reg < 0xb4)//FB,ALGO
    {
        insChn[ch].algo = val & 7;
        insChn[ch].feedback = (val >> 3) & 7;
    }
}



bool process_file(const char* filename, int fileId)
{
    unsigned char buf[4];
    unsigned char* vgm;
    gzFile zfile;
    FILE* file;
    int pp, tag, size;

    //get uncompressed file size

    file = fopen(filename, "rb");
    if (!file)
    {
        printf("ERR: Can't open file '%s'\n", filename);
        return false;
    }
    fread(buf, 4, 1, file);
    if (memcmp(buf, "Vgm ", 4))//gz file, get size from last four bytes
    {
        fseek(file, -4, SEEK_END);
        fread(buf, 4, 1, file);
        size = buf[0] + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24);
    }
    else//uncompressed file, just get file size
    {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
    }
    fclose(file);

    zfile = gzopen(filename, "rb");
    if (!zfile)
    {
        printf("ERR: Can't open file '%s'\n", filename);
        return false;
    }

    vgm = (unsigned char*)malloc(size);
    if (!vgm)
    {
        printf("ERR: Can't alloc memory for file '%s'\n", filename);
        gzclose(zfile);
        return false;
    }

    gzread(zfile, vgm, size);
    gzclose(zfile);

    if (memcmp(vgm, "Vgm ", 4))
    {
        printf("ERR: No VGM found in file '%s'\n", filename);
        free(vgm);
        return false;
    }

    printf("OK: Processing '%s' (VGM v%i.%i)\n", filename, vgm[9], vgm[8]);

    if (vgm[9] <= 1 && vgm[8] < 50) pp = 0x40; else pp = (vgm[0x34] + (vgm[0x35] << 8) + (vgm[0x36] << 16) + (vgm[0x37] << 24)) + 0x34;

    dacOn = false;

    while (pp < size)
    {
        tag = vgm[pp++];
        switch (tag)
        {
        case 0x4f://game gear stereo
        case 0x50://PSG
        case 0x62://wait 735
        case 0x63://wait 882
            pp++;
            break;
        case 0x51://YM2413
        case 0x54://YM2151
        case 0x61://wait N
            pp += 2;
            break;
        case 0x66://EOF
            pp = size;
            break;
        case 0x67://data block
            pp = pp + 2 + vgm[pp + 2] + (vgm[pp + 3] << 8) + (vgm[pp + 4] << 16) + (vgm[pp + 5] << 24);
            break;
        case 0xe0://PCM seek
            pp += 4;
            break;

        case 0x52://YM2612 bank 0
        case 0x53://YM2612 bank 1
            write_fm(tag - 0x52, vgm[pp], vgm[pp + 1], fileId);
            pp += 2;
            break;
        }
        if (tag >= 0x30 && tag <= 0x4e) pp++;
        if (tag >= 0x55 && tag <= 0x5f) pp += 2;
        if (tag >= 0xa0 && tag <= 0xbf) pp += 2;
        if (tag >= 0xc0 && tag <= 0xdf) pp += 3;
        if (tag >= 0xe1 && tag <= 0xff) pp += 4;
    }

    free(vgm);

    return true;
}



bool is_tag(char* txt, char* tag)
{
    int i, len, ch1, ch2;

    len = (int)strlen(tag);

    for (i = 0; i < len; i++)
    {
        ch1 = txt[i];
        ch2 = tag[i];
        if (ch1 >= 'A' && ch1 <= 'Z') ch1 += 0x20;
        if (ch2 >= 'A' && ch2 <= 'Z') ch2 += 0x20;
        if (ch1 != ch2) return false;
    }

    return true;
}



bool check_ext(const char* txt, char* ext)
{
    return is_tag((char*)txt + strlen(txt) - 3, ext);
}



void add_file(const char* filename, bool dir)
{
    char name[PATH_MAX];
    int i;

    if (!dir && !check_ext(filename, const_cast<char*>("vgm")) && !check_ext(filename, const_cast<char*>("vgz"))) return;//add only *.vgm and *.vgz files

    if (fileListCnt >= fileListSize)
    {
        fileListSize += 32;
        fileList = (fileListStruct*)realloc(fileList, fileListSize * sizeof(fileListStruct));
        for (i = fileListCnt; i < fileListSize; i++)
        {
            fileList[i].dir = false;
            fileList[i].name = NULL;
            fileList[i].path = NULL;
        }
    }

    fileList[fileListCnt].path = (char*)malloc(strlen(filename) + 1);
    strcpy(fileList[fileListCnt].path, filename);

    if (!dir)//prepare filename without path to use as output prefix
    {
        strcpy(name, filename);
        for (i = (int)strlen(name); i >= 0; i--)
        {
            if (name[i] == '/' || name[i] == '\\')
            {
                strcpy(name, name + i + 1);
                break;
            }
        }
        for (i = 0; i < (int)strlen(name); i++)
        {
            if (name[i] == ' ') name[i] = '_';
            if (name[i] == '.') name[i] = 0;
        }
        strcat(name, "_");
        fileList[fileListCnt].name = (char*)malloc(strlen(name) + 1);
        strcpy(fileList[fileListCnt].name, name);
    }
    else
    {
        fileList[fileListCnt].name = NULL;
    }

    fileList[fileListCnt].dir = dir;
    fileListCnt++;
}



bool add_dir_file(fs::path* findData)
{
    char path[PATH_MAX];
    bool dir;

    if (strlen(findData->string().c_str()) <= 0) return true;
    if (strcmp(findData->string().c_str(), ".") == 0) return true;
    if (strcmp(findData->string().c_str(), "..") == 0) return true;

    getcwd(path, PATH_MAX);
    strcat(path, DIRECTORY_SEPARATOR);
    strcat(path, findData->string().c_str());
    if (fs::is_directory(*findData)) dir = true; else dir = false;
    add_file(path, dir);

    return false;
}



void add_dir(const char* path)
{
    char root[PATH_MAX];
    bool noAdded;
    int curCount, curBegin;
    int i;

    strcpy(root, path);
    //root[strlen(root)-1]=0;
    add_file(root, true);//add root directory

    noAdded = false;
    curBegin = 0;

    while (!noAdded)
    {
        noAdded = true;
        curCount = fileListCnt - curBegin;
        for (i = curBegin; i < curBegin + curCount; i++)
        {
            if (fileList[i].dir)
            {
                chdir(fileList[i].path);
                fs::path targetDir(".");
                fs::directory_iterator curr_itr(targetDir), end_itr;

                if (curr_itr == end_itr)
                {
                    for (i = 0; i < fileListCnt; ++i)
                    {
                        if (fileList[i].path) free(fileList[i].path);
                        if (fileList[i].name) free(fileList[i].name);
                    }
                    free(fileList);
                    fprintf(stderr, "ERR: No files in directory or other fatal error\n");
                    exit(0);
                }

                for (; curr_itr != end_itr; ++curr_itr)
                {
                    if (!is_directory(curr_itr->status()))
                    {
                        auto tmp = fs::path(curr_itr->path());
                        noAdded = add_dir_file(&tmp);
                    }
                }
            }
        }
        curBegin += curCount;
    }
}



int main(int argc, char* argv[])
{
    char insname[PATH_MAX], workdir[PATH_MAX];
    int i, j, id, slot, vol, mvol, uqCnt;

    if (argc < 2)
    {
        printf("VGM2TFI v1.0 by Shiru, 26.01.08\n");
        printf("Converted to POSIX/Portable C(++) by Consolethinks, 31.03.2019\n");
        printf("USAGE: vgm2tfi filename.vgm [name2.vgm ..] to process one or few files\n");
        printf("       vgm2tfi directory1 [directory2, ..] to process whole directory(ies) with all subdirectories\n");
        return 0;
    }

    //remember work directory

    getcwd(workdir, PATH_MAX);

    //prepare file list

    fileListCnt = 0;
    fileListSize = 32;
    fileList = (fileListStruct*)malloc(fileListSize * sizeof(fileListStruct));
    for (i = fileListCnt; i < fileListSize; i++)
    {
        fileList[i].dir = false;
        fileList[i].name = NULL;
        fileList[i].path = NULL;
    }

    /*if(argc>2)
    {
        for(i=1;i<argc;i++) add_file(argv[i],false);
    }
    else
    {
        if(argv[1][strlen(argv[1])-1]=='*') add_dir(argv[1]); else add_file(argv[1],false);
    }*/

    for (i = 1; i < argc; ++i)
    {
        if (fs::is_directory(fs::path(argv[i]))) add_dir(argv[i]);
        else add_file(argv[i], false);
    }

    //init instruments list

    insListCnt = 0;
    insListSize = 16;
    insList = (instrumentStruct*)malloc(insListSize * sizeof(instrumentStruct));

    //for each file

    for (j = 0; j < fileListCnt; j++)
    {
        if (fileList[j].dir) continue;
        for (i = 0; i < 6; i++)
        {
            memset(&insChn[i], 0x00, sizeof(instrumentStruct));
            memset(&insChnPrev[i], 0xff, sizeof(instrumentStruct));
        }
        process_file(fileList[j].path, j);
    }

    //search for same instruments with different volume

    for (i = 0; i < insListCnt; i++) insList[i].id = -1;

    uqCnt = 0;
    for (i = 0; i < insListCnt; i++)
    {
        if (insList[i].id < 0)
        {
            insList[i].id = uqCnt++;
            for (j = 0; j < insListCnt; j++)
            {
                if (i == j) continue;
                if (ins_compare_novol(&insList[i], &insList[j])) insList[j].id = insList[i].id;
            }
        }
    }

    //search for most loud of same instruments

    for (j = 0; j < uqCnt; j++)
    {
        id = -1;
        mvol = 0;
        for (i = 0; i < insListCnt; i++)
        {
            if (insList[i].id == j)
            {
                slot = ins_slot(&insList[i]);
                vol = 0;
                if (slot & 1) vol += (127 - insList[i].op[0].totalLevel);
                if (slot & 2) vol += (127 - insList[i].op[1].totalLevel);
                if (slot & 4) vol += (127 - insList[i].op[2].totalLevel);
                if (slot & 8) vol += (127 - insList[i].op[3].totalLevel);

                if (vol > mvol)
                {
                    id = i;
                    mvol = vol;
                }
            }
        }
        if (id >= 0)
        {
            for (i = 0; i < insListCnt; i++)
            {
                if (insList[i].id == j)
                {
                    if (i != id) insList[i].id = -1;
                }
            }
        }
    }

    //save instruments

    printf("OK: %i instruments were found\n", uqCnt);

    for (j = 0; j < uqCnt; j++)
    {
        for (i = 0; i < insListCnt; i++)
        {
            if (insList[i].id == j)
            {
                sprintf(insname, "%s%s%s%i.tfi", workdir, DIRECTORY_SEPARATOR, fileList[insList[i].fileId].name, j);
                if (!ins_save(&insList[i], insname))
                {
                    printf("ERR: Can't save file '%s'\n", insname);
                    break;
                }
            }
        }
    }

    //free memory

    free(insList);

    for (i = 0; i < fileListCnt; i++)
    {
        if (fileList[i].path) free(fileList[i].path);
        if (fileList[i].name) free(fileList[i].name);
    }
    free(fileList);

    return 0;
}