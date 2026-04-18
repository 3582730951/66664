#pragma once

#include <string_view>

#include <vmp/runtime/strings/cipher.h>

#define VMP_STRING_USE(string_id, var_name)                                                                            \
  if (auto _vmp_transient_view_##var_name = ::vmp::runtime::strings::current_string_pool().decrypt(string_id); true) \
    for (bool _vmp_once_##var_name = true; _vmp_once_##var_name; _vmp_once_##var_name = false)                       \
      for (std::string_view var_name(_vmp_transient_view_##var_name.data(), _vmp_transient_view_##var_name.size());  \
           _vmp_once_##var_name; _vmp_once_##var_name = false)
