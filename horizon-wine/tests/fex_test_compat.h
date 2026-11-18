/* The FEX core checks (check-fex-core.sh) build against FEX-2609 with
 * horizon.patch or FEX-2611.1 with horizon-2611.patch. These pick, at compile
 * time, the API of the release in WINE_NX_FEX_DIR. */
#pragma once

#include <cstddef>
#include <type_traits>

/* FEX-2611 keeps the host type with the process's features. */
template <typename Features, typename Type>
inline void fex_test_set_host_type( Features &features, Type type )
{
    if constexpr (requires { features.ProcessFeatures.HostType; }) features.ProcessFeatures.HostType = type;
    else features.HostType = type;
}

/* FEX-2611's code buffer is told whether to name its mapping. The buffer can
 * be neither copied nor moved: it is returned as it is made. */
template <typename Buffer>
inline Buffer fex_test_code_buffer( std::size_t size )
{
    if constexpr (std::is_constructible_v<Buffer, std::size_t, bool, std::size_t>) return Buffer( size, false );
    else return Buffer( size );
}

/* FEX-2611 keeps SVE with the process's features, behind accessors. */
template <typename Features>
inline bool fex_test_sve128( const Features &features )
{
    if constexpr (requires { features.ProcessFeatures.SupportsSVE128; }) return features.ProcessFeatures.SupportsSVE128;
    else return features.SupportsSVE128;
}

template <typename Features>
inline bool fex_test_sve256( const Features &features )
{
    if constexpr (requires { features.ProcessFeatures.SupportsSVE256; }) return features.ProcessFeatures.SupportsSVE256;
    else return features.SupportsSVE256;
}

/* A host check that ends its guest code at HLT (EnableExitOnHLT) needs the
 * fault path: FEX-2611's fault-free path for ARM64EC calls the frontend's
 * SynchronousExceptionHandler instead, which only the ARM64EC module installs. */
template <typename Features>
inline void fex_test_fault_path( Features &features )
{
    if constexpr (requires { features.ProcessFeatures.SupportsFaultFreeSynchronousFaults; })
        features.ProcessFeatures.SupportsFaultFreeSynchronousFaults = false;
}
