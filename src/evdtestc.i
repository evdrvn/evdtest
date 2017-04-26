%module evdtestc
%{
#include "evdtest.h"
%}
%include <typemaps.i>
%apply SWIGTYPE** OUTPUT{evdsptc_event_t** event};
%apply SWIGTYPE* DISOWN{evdsptc_event_t* event};
%typemap(out) evdtest_error_t {
    lua_pushnumber(L, (int)$1);
    SWIG_arg++;
}
%typemap(out) evdsptc_error_t {
    lua_pushnumber(L, (int)$1);
    SWIG_arg++;
}
%include "evdtest.h"
