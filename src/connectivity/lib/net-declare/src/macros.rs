// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;
#[macro_use]
extern crate quote;

use proc_macro2::TokenStream;
use std::fmt::Formatter;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6};
use std::num::ParseIntError;
use std::str::FromStr;

/// Declares a proc_macro with `name` using `generator` to generate any of `ty`.
macro_rules! declare_macro {
    ($name:ident, $generator:ident, $($ty:ident),+) => {
        #[proc_macro]
        pub fn $name(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
            Emitter::<$generator, $($ty),+>::emit(input).into()
        }
    }
}

/// Empty slot in an [`Emitter`].
struct Skip;

/// The mock error returned by [`Skip`].
#[derive(Debug)]
struct SkipError;

impl std::fmt::Display for SkipError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}
impl std::error::Error for SkipError {}

impl FromStr for Skip {
    type Err = SkipError;

    fn from_str(_s: &str) -> Result<Self, Self::Err> {
        Err(SkipError {})
    }
}

/// Generates a [`TokenStream`] representation of `T`.
trait Generator<T> {
    /// Generates the [`TokenStream`] for `input`.
    fn generate(input: T) -> TokenStream;

    /// Get an optional string representation of type `T`.
    ///
    /// Used in error reporting when parsing fails.
    fn type_str() -> Option<&'static str> {
        Some(std::any::type_name::<T>())
    }
}

impl<O> Generator<Skip> for O {
    fn generate(_input: Skip) -> TokenStream {
        unreachable!()
    }

    fn type_str() -> Option<&'static str> {
        None
    }
}

/// Attempts to emit the resulting [`TokenStream`] for `input` parsed as `T`.
fn try_emit<G, T>(input: &str) -> Result<TokenStream, T::Err>
where
    G: Generator<T>,
    T: FromStr,
{
    Ok(G::generate(T::from_str(input)?))
}

/// Provides common structor for parsing types from [`TokenStream`]s and
/// emitting the resulting [`TokenStream`].
///
/// `Emitter` can parse any of `Tn` and pass into a [`Generator`] `G`, encoding
/// the common logic for declarations build from string parsing at compile-time.
struct Emitter<G, T1 = Skip, T2 = Skip, T3 = Skip, T4 = Skip> {
    _g: std::marker::PhantomData<G>,
    _t1: std::marker::PhantomData<T1>,
    _t2: std::marker::PhantomData<T2>,
    _t3: std::marker::PhantomData<T3>,
    _t4: std::marker::PhantomData<T4>,
}

impl<G, T1, T2, T3, T4> Emitter<G, T1, T2, T3, T4>
where
    G: Generator<T1> + Generator<T2> + Generator<T3> + Generator<T4>,
    T1: FromStr,
    T2: FromStr,
    T3: FromStr,
    T4: FromStr,
    T1::Err: std::error::Error,
    T2::Err: std::error::Error,
    T3::Err: std::error::Error,
    T4::Err: std::error::Error,
{
    /// Emits the resulting [`TokenStream`] (or an error one) after attempting
    /// to parse `input` into all of the `Emitter`'s types sequentially.
    fn emit(input: proc_macro::TokenStream) -> TokenStream {
        // Always require a string literal.
        let input = proc_macro2::TokenStream::from(input);
        let s = match syn::parse2::<syn::LitStr>(input.clone()) {
            Ok(s) => s.value(),
            Err(e) => return e.to_compile_error().into(),
        };
        match try_emit::<G, T1>(&s)
            .or_else(|e1| try_emit::<G, T2>(&s).map_err(|e2| (e1, e2)))
            .or_else(|(e1, e2)| try_emit::<G, T3>(&s).map_err(|e3| (e1, e2, e3)))
            .or_else(|(e1, e2, e3)| try_emit::<G, T4>(&s).map_err(|e4| (e1, e2, e3, e4)))
        {
            Ok(ts) => ts,
            Err((e1, e2, e3, e4)) => syn::Error::new_spanned(
                input,
                format!("failed to parse as {}", Self::error_str(&e1, &e2, &e3, &e4)),
            )
            .to_compile_error()
            .into(),
        }
    }

    /// Get the error string reported to the compiler when parsing fails with
    /// this `Emitter`.
    fn error_str(
        e1: &dyn std::error::Error,
        e2: &dyn std::error::Error,
        e3: &dyn std::error::Error,
        e4: &dyn std::error::Error,
    ) -> String {
        [
            (<G as Generator<T1>>::type_str(), e1),
            (<G as Generator<T2>>::type_str(), e2),
            (<G as Generator<T3>>::type_str(), e3),
            (<G as Generator<T4>>::type_str(), e4),
        ]
        .iter()
        .filter_map(|(ts, e)| ts.map(|t| format!("{}: \"{}\"", t, e)))
        .collect::<Vec<_>>()
        .join(", or ")
    }
}

/// Generator for `std` types.
enum StdGen {}

impl Generator<IpAddr> for StdGen {
    fn generate(input: IpAddr) -> TokenStream {
        let (t, inner) = match input {
            IpAddr::V4(v4) => (quote! { V4 }, Self::generate(v4)),
            IpAddr::V6(v6) => (quote! { V6 }, Self::generate(v6)),
        };
        quote! {
            std::net::IpAddr::#t(#inner)
        }
    }
}

impl Generator<Ipv4Addr> for StdGen {
    fn generate(input: Ipv4Addr) -> TokenStream {
        let octets = input.octets();
        quote! {
            std::net::Ipv4Addr::new(#(#octets),*)
        }
    }
}

impl Generator<Ipv6Addr> for StdGen {
    fn generate(input: Ipv6Addr) -> TokenStream {
        let segments = input.segments();
        quote! {
            std::net::Ipv6Addr::new(#(#segments),*)
        }
    }
}

impl Generator<SocketAddr> for StdGen {
    fn generate(input: SocketAddr) -> TokenStream {
        let (t, inner) = match input {
            SocketAddr::V4(v4) => (quote! { V4 }, Self::generate(v4)),
            SocketAddr::V6(v6) => (quote! { V6 }, Self::generate(v6)),
        };
        quote! {
            std::net::SocketAddr::#t(#inner)
        }
    }
}

impl Generator<SocketAddrV4> for StdGen {
    fn generate(input: SocketAddrV4) -> TokenStream {
        let addr = Self::generate(input.ip().clone());
        let port = input.port();
        quote! {
            std::net::SocketAddrV4::new(#addr, #port)
        }
    }
}

impl Generator<SocketAddrV6> for StdGen {
    fn generate(input: SocketAddrV6) -> TokenStream {
        let addr = Self::generate(input.ip().clone());
        let port = input.port();
        let flowinfo = input.flowinfo();
        let scope_id = input.scope_id();
        quote! {
            std::net::SocketAddrV6::new(#addr, #port, #flowinfo, #scope_id)
        }
    }
}

declare_macro!(std_ip, StdGen, IpAddr);
declare_macro!(std_ip_v4, StdGen, Ipv4Addr);
declare_macro!(std_ip_v6, StdGen, Ipv6Addr);
declare_macro!(std_socket_addr, StdGen, SocketAddr, SocketAddrV4);
declare_macro!(std_socket_addr_v4, StdGen, SocketAddrV4);
declare_macro!(std_socket_addr_v6, StdGen, SocketAddrV6);

/// Generator for FIDL types.
enum FidlGen {}

impl Generator<IpAddr> for FidlGen {
    fn generate(input: IpAddr) -> TokenStream {
        let (t, inner) = match input {
            IpAddr::V4(v4) => (quote! { Ipv4 }, Self::generate(v4)),
            IpAddr::V6(v6) => (quote! { Ipv6 }, Self::generate(v6)),
        };
        quote! {
            fidl_fuchsia_net::IpAddress::#t(#inner)
        }
    }
}

impl Generator<Ipv4Addr> for FidlGen {
    fn generate(input: Ipv4Addr) -> TokenStream {
        let octets = input.octets();
        quote! {
            fidl_fuchsia_net::Ipv4Address{ addr: [#(#octets),*]}
        }
    }
}

impl Generator<Ipv6Addr> for FidlGen {
    fn generate(input: Ipv6Addr) -> TokenStream {
        let octets = input.octets();
        quote! {
            fidl_fuchsia_net::Ipv6Address{ addr: [#(#octets),*]}
        }
    }
}

impl Generator<SocketAddr> for FidlGen {
    fn generate(input: SocketAddr) -> TokenStream {
        let (t, inner) = match input {
            SocketAddr::V4(v4) => (quote! { Ipv4 }, Self::generate(v4)),
            SocketAddr::V6(v6) => (quote! { Ipv6 }, Self::generate(v6)),
        };
        quote! {
            fidl_fuchsia_net::SocketAddress::#t(#inner)
        }
    }
}

impl Generator<SocketAddrV4> for FidlGen {
    fn generate(input: SocketAddrV4) -> TokenStream {
        let addr = Self::generate(input.ip().clone());
        let port = input.port();
        quote! {
            fidl_fuchsia_net::Ipv4SocketAddress {
                address: #addr,
                port: #port
            }
        }
    }
}

impl Generator<SocketAddrV6> for FidlGen {
    fn generate(input: SocketAddrV6) -> TokenStream {
        let addr = Self::generate(input.ip().clone());
        let port = input.port();
        let scope_id = u64::from(input.scope_id());
        quote! {
            fidl_fuchsia_net::Ipv6SocketAddress {
                address: #addr,
                port: #port,
                zone_index: #scope_id
            }
        }
    }
}

/// Helper struct to parse Mac addresses from string.
#[derive(Default)]
struct MacAddress([u8; 6]);

#[derive(thiserror::Error, Debug)]
enum MacParseError {
    #[error("invalid length for MacAddress, should be 6")]
    InvalidLength,
    #[error("invalid byte length (\"{0}\") in MacAddress, should be 2")]
    InvalidByte(String),
    #[error("failed to parse byte \"{0}\": {1}")]
    IntError(String, ParseIntError),
}

impl FromStr for MacAddress {
    type Err = MacParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut mac = Self::default();
        let Self(octets) = &mut mac;
        let mut parse_iter = s.split(':');
        let mut save_iter = octets.iter_mut();
        loop {
            match (parse_iter.next(), save_iter.next()) {
                (Some(s), Some(b)) => {
                    if s.len() != 2 {
                        return Err(MacParseError::InvalidByte(s.to_string()));
                    }
                    *b = u8::from_str_radix(s, 16)
                        .map_err(|e| MacParseError::IntError(s.to_string(), e))?;
                }
                (None, Some(_)) | (Some(_), None) => break Err(MacParseError::InvalidLength),
                (None, None) => break Ok(mac),
            }
        }
    }
}

impl Generator<MacAddress> for FidlGen {
    fn generate(input: MacAddress) -> TokenStream {
        let MacAddress(octets) = input;
        quote! {
            fidl_fuchsia_net::MacAddress {
                octets: [#(#octets),*]
            }
        }
    }
}

/// Helper struct to parse Cidr addresses from string.
struct CidrAddress {
    address: std::net::IpAddr,
    prefix: u8,
}

#[derive(thiserror::Error, Debug)]
enum CidrParseError {
    #[error("missing address")]
    MissingIp,
    #[error("missing prefix length")]
    MissingPrefix,
    #[error("unexpected trailing data \"{0}\"")]
    TrailingInformation(String),
    #[error("failed to parse IP \"{0}\": {1}")]
    IpParseError(String, std::net::AddrParseError),
    #[error("failed to parse prefix \"{0}\": {1}")]
    PrefixParseError(String, std::num::ParseIntError),
    #[error("bad prefix value {0} for {1}")]
    BadPrefix(u8, std::net::IpAddr),
}

impl FromStr for CidrAddress {
    type Err = CidrParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut parse_iter = s.split('/');
        let ip_str = parse_iter.next().ok_or(CidrParseError::MissingIp)?;
        let prefix_str = parse_iter.next().ok_or(CidrParseError::MissingPrefix)?;
        if let Some(trailing) = parse_iter.next() {
            return Err(CidrParseError::TrailingInformation(trailing.to_string()));
        }
        let address = std::net::IpAddr::from_str(ip_str)
            .map_err(|e| CidrParseError::IpParseError(ip_str.to_string(), e))?;
        let prefix = u8::from_str_radix(prefix_str, 10)
            .map_err(|e| CidrParseError::PrefixParseError(prefix_str.to_string(), e))?;
        let addr_len = 8 * match address {
            std::net::IpAddr::V4(a) => a.octets().len(),
            std::net::IpAddr::V6(a) => a.octets().len(),
        };
        if usize::from(prefix) > addr_len {
            return Err(CidrParseError::BadPrefix(prefix, address));
        }
        Ok(CidrAddress { address, prefix })
    }
}

impl Generator<CidrAddress> for FidlGen {
    fn generate(input: CidrAddress) -> TokenStream {
        let CidrAddress { address, prefix } = input;
        let address = Self::generate(address);
        quote! {
            fidl_fuchsia_net::Subnet {
                addr: #address,
                prefix_len: #prefix
            }
        }
    }
}

declare_macro!(fidl_ip, FidlGen, IpAddr);
declare_macro!(fidl_ip_v4, FidlGen, Ipv4Addr);
declare_macro!(fidl_ip_v6, FidlGen, Ipv6Addr);
declare_macro!(fidl_socket_addr, FidlGen, SocketAddr);
declare_macro!(fidl_socket_addr_v4, FidlGen, SocketAddrV4);
declare_macro!(fidl_socket_addr_v6, FidlGen, SocketAddrV6);
declare_macro!(fidl_mac, FidlGen, MacAddress);
declare_macro!(fidl_subnet, FidlGen, CidrAddress);

/// Generator for net-types types.
enum NetGen {}

impl Generator<IpAddr> for NetGen {
    fn generate(input: IpAddr) -> TokenStream {
        let (t, inner) = match input {
            IpAddr::V4(v4) => (quote! { V4 }, Self::generate(v4)),
            IpAddr::V6(v6) => (quote! { V6 }, Self::generate(v6)),
        };
        quote! {
            net_types::ip::IpAddr::#t(#inner)
        }
    }
}

impl Generator<Ipv4Addr> for NetGen {
    fn generate(input: Ipv4Addr) -> TokenStream {
        let octets = input.octets();
        quote! {
            net_types::ip::Ipv4Addr::new([#(#octets),*])
        }
    }
}

impl Generator<Ipv6Addr> for NetGen {
    fn generate(input: Ipv6Addr) -> TokenStream {
        let octets = input.octets();
        quote! {
            net_types::ip::Ipv6Addr::from_bytes([#(#octets),*])
        }
    }
}

impl Generator<MacAddress> for NetGen {
    fn generate(input: MacAddress) -> TokenStream {
        let MacAddress(octets) = input;
        quote! {
            net_types::ethernet::Mac::new([#(#octets),*])
        }
    }
}

declare_macro!(net_ip, NetGen, IpAddr);
declare_macro!(net_ip_v4, NetGen, Ipv4Addr);
declare_macro!(net_ip_v6, NetGen, Ipv6Addr);
declare_macro!(net_mac, NetGen, MacAddress);
