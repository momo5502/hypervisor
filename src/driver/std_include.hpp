#pragma once

#include <ntddk.h>
#include <intrin.h>
#include <Ntstrsafe.h>

#pragma warning(push)
#pragma warning(disable: 4201)
#include <ia32.hpp>
#pragma warning(pop)

#define STRINGIFY_(a) #a
#define STRINGIFY(a) STRINGIFY_(a)

#include "stdint.hpp"
#include "nt_ext.hpp"
#include "new.hpp"
#include "exception.hpp"
