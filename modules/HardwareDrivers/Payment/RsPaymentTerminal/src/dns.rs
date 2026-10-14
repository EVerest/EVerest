//! Looks up the Feig terminal via mDNS.
//!
//! The terminal announces itself as `cvend-<feig_serial>.local`. We send a
//! one-shot query to the mDNS group from an ephemeral port, so the terminal
//! answers directly to us - see
//! [RFC 6762, section 6.7](https://www.rfc-editor.org/rfc/rfc6762#section-6.7).
//!
//! The answer comes from the terminal's own address, but hickory's UDP client
//! drops answers which don't come from the queried address. The
//! [MdnsUdpSocket] works around that. We can't use hickory's multicast client
//! instead: in its one-shot mode it spins in a busy loop and never times out
//! (hickory-proto 0.25.2).
use hickory_client::client::{Client, ClientHandle};
use hickory_client::proto::rr::{DNSClass, Name, RData, RecordType};
use hickory_client::proto::runtime::{RuntimeProvider, TokioRuntimeProvider, TokioTime};
use hickory_client::proto::udp::{DnsUdpSocket, UdpClientStream};
use std::future::Future;
use std::io;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::pin::Pin;
use std::str::FromStr;
use std::task::{ready, Context, Poll};
use std::time::Duration;

/// Address for the mDns as defined [here](https://www.rfc-editor.org/rfc/rfc6762).
const MDNS_IPV4: SocketAddr = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(224, 0, 0, 251)), 5353);

/// How long to wait for an answer. The query is sent only once, so waiting
/// longer than a responder on the local link needs does not help - the caller
/// rather retries. Keep it short, since the module blocks on it at startup.
const MDNS_TIMEOUT: Duration = Duration::from_secs(5);

/// UDP socket which reports every answer as coming from [MDNS_IPV4].
struct MdnsUdpSocket(tokio::net::UdpSocket);

impl DnsUdpSocket for MdnsUdpSocket {
    type Time = TokioTime;

    fn poll_recv_from(
        &self,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<(usize, SocketAddr)>> {
        let mut buf = tokio::io::ReadBuf::new(buf);
        let address = ready!(self.0.poll_recv_from(cx, &mut buf))?;
        log::debug!("Received an answer from {address:?}");
        Poll::Ready(Ok((buf.filled().len(), MDNS_IPV4)))
    }

    fn poll_send_to(
        &self,
        cx: &mut Context<'_>,
        buf: &[u8],
        target: SocketAddr,
    ) -> Poll<io::Result<usize>> {
        self.0.poll_send_to(cx, buf, target)
    }
}

/// The tokio runtime of hickory, but with the [MdnsUdpSocket].
#[derive(Clone, Default)]
struct MdnsRuntimeProvider(TokioRuntimeProvider);

impl RuntimeProvider for MdnsRuntimeProvider {
    type Handle = <TokioRuntimeProvider as RuntimeProvider>::Handle;
    type Timer = TokioTime;
    type Udp = MdnsUdpSocket;
    type Tcp = <TokioRuntimeProvider as RuntimeProvider>::Tcp;

    fn create_handle(&self) -> Self::Handle {
        self.0.create_handle()
    }

    fn connect_tcp(
        &self,
        server_addr: SocketAddr,
        bind_addr: Option<SocketAddr>,
        timeout: Option<Duration>,
    ) -> Pin<Box<dyn Send + Future<Output = io::Result<Self::Tcp>>>> {
        self.0.connect_tcp(server_addr, bind_addr, timeout)
    }

    fn bind_udp(
        &self,
        local_addr: SocketAddr,
        _server_addr: SocketAddr,
    ) -> Pin<Box<dyn Send + Future<Output = io::Result<Self::Udp>>>> {
        Box::pin(async move {
            let socket = tokio::net::UdpSocket::bind(local_addr).await?;
            Ok(MdnsUdpSocket(socket))
        })
    }
}

/// Returns the ip-address of the Feig terminal with the given serial.
pub async fn resolve(feig_serial: &str) -> anyhow::Result<Ipv4Addr> {
    let stream = UdpClientStream::builder(MDNS_IPV4, MdnsRuntimeProvider::default())
        .with_timeout(Some(MDNS_TIMEOUT))
        .build();
    let (mut client, bg) = Client::connect(stream).await?;
    tokio::spawn(bg);

    let name = Name::from_str(&format!("cvend-{feig_serial}.local"))?;
    let response = client
        .query(name.clone(), DNSClass::IN, RecordType::A)
        .await?;

    // Return the first ip-address in the response.
    response
        .answers()
        .iter()
        .find_map(|answer| match answer.data() {
            RData::A(ip) => Some(ip.0),
            _ => None,
        })
        .ok_or_else(|| anyhow::anyhow!("No address for {name}"))
}
