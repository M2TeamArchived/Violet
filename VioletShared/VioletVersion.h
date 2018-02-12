/******************************************************************************
Project: VioletShared
Description: Violet Version Definition
File Name: VioletVersion.h
License: The MIT License
******************************************************************************/

//#include "CIBuild.h"

#ifndef VIOLET_VER
#define VIOLET_VER

#define VIOLET_VER_MAJOR 0
#define VIOLET_VER_MINOR 1
#define VIOLET_VER_BUILD 1
#define VIOLET_VER_REV 0
#endif

#ifndef VIOLET_VER_FMT_COMMA
#define VIOLET_VER_FMT_COMMA VIOLET_VER_MAJOR,VIOLET_VER_MINOR,VIOLET_VER_BUILD,VIOLET_VER_REV
#endif

#ifndef VIOLET_VER_FMT_DOT
#define VIOLET_VER_FMT_DOT VIOLET_VER_MAJOR.VIOLET_VER_MINOR.VIOLET_VER_BUILD.VIOLET_VER_REV
#endif


#ifndef MACRO_TO_STRING
#define _MACRO_TO_STRING(arg) L#arg
#define MACRO_TO_STRING(arg) _MACRO_TO_STRING(arg)
#endif

#ifndef VIOLET_VERSION
#define VIOLET_VERSION VIOLET_VER_FMT_COMMA
#endif

#ifndef _VIOLET_VERSION_STRING_
#define _VIOLET_VERSION_STRING_ MACRO_TO_STRING(VIOLET_VER_FMT_DOT)
#endif

#ifndef VIOLET_VERSION_STRING
#ifdef VIOLET_CI_BUILD
#define VIOLET_VERSION_STRING _VIOLET_VERSION_STRING_ L" " VIOLET_CI_BUILD
#else
#define VIOLET_VERSION_STRING _VIOLET_VERSION_STRING_
#endif
#endif
