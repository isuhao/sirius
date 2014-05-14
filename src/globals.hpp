#ifndef SIRIUS_GLOBAL_HPP
#define SIRIUS_GLOBAL_HPP

#include <string>
#include <vector>
#include <fstream>
#include "CppJieba/Limonp/str_functs.hpp"

namespace Sirius
{
    using namespace std;

    const size_t LINE_COLLUMN_N = 3;
    const size_t POST_COLLUMN_N = 3;
    const size_t TITLE_TOP_N = 5;
    const size_t CONTENT_TOP_N = 1;

    typedef size_t TokenidType;
    typedef size_t DocidType;

    struct RequestData
    {
        size_t id;
        string title;
        string content;
    };
    struct ResponseDataUnit
    {
        size_t id;
        string title;
        double similar;
    };
    inline ostream& operator << (ostream& os, const RequestDataUnit& reqdata)
    {
        return os << Limonp::string_format("{id:%u, title: %s, content: %s}", reqdata.id, reqdata.title.c_str(), reqdata.content.c_str());
    }
    typedef vector<ResponseDataUnit> ResponseData; 

    struct DocInfo
    {
        size_t id;
        size_t offset;
        size_t length;
        string title;
        string content;
        vector<TokenidType>  titleTokens;
        vector<TokenidType> contentTokens;
    };
}

#endif
