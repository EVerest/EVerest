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

/// UDP socket which reports every answer as coming from the name server.
struct MdnsUdpSocket {
    socket: tokio::net::UdpSocket,
    name_server: SocketAddr,
}

impl DnsUdpSocket for MdnsUdpSocket {
    type Time = TokioTime;

    fn poll_recv_from(
        &self,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<io::Result<(usize, SocketAddr)>> {
        let (len, address) = ready!(DnsUdpSocket::poll_recv_from(&self.socket, cx, buf))?;
        log::debug!("Received an answer from {address:?}");
        Poll::Ready(Ok((len, self.name_server)))
    }

    fn poll_send_to(
        &self,
        cx: &mut Context<'_>,
        buf: &[u8],
        target: SocketAddr,
    ) -> Poll<io::Result<usize>> {
        DnsUdpSocket::poll_send_to(&self.socket, cx, buf, target)
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
        server_addr: SocketAddr,
    ) -> Pin<Box<dyn Send + Future<Output = io::Result<Self::Udp>>>> {
        // The stock provider only binds and doesn't connect the socket - we
        // don't know the address which will answer.
        let socket = self.0.bind_udp(local_addr, server_addr);
        Box::pin(async move {
            Ok(MdnsUdpSocket {
                socket: socket.await?,
                name_server: server_addr,
            })
        })
    }
}

/// Returns the ip-address of the Feig terminal with the given serial.
pub async fn resolve(feig_serial: &str) -> anyhow::Result<Ipv4Addr> {
    query_a(
        MDNS_IPV4,
        &format!("cvend-{feig_serial}.local"),
        MDNS_TIMEOUT,
    )
    .await
}

/// Queries the A record of `name` at the `name_server` and returns the first
/// ip-address in the response.
async fn query_a(
    name_server: SocketAddr,
    name: &str,
    timeout: Duration,
) -> anyhow::Result<Ipv4Addr> {
    let stream = UdpClientStream::builder(name_server, MdnsRuntimeProvider::default())
        .with_timeout(Some(timeout))
        .build();
    let (mut client, bg) = Client::connect(stream).await?;
    tokio::spawn(bg);

    let name = Name::from_str(name)?;
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

#[cfg(test)]
mod test {
    use super::*;
    use hickory_client::proto::op::{Message, MessageType};
    use hickory_client::proto::rr::rdata::A;
    use hickory_client::proto::rr::Record;

    /// Fake name-server which answers every A query with `ip` - but sends the
    /// reply from a second socket, like a host answering an mDns query does.
    /// With `junk_first` a datagram which is not a dns message precedes the
    /// reply.
    async fn spawn_responder(ip: Ipv4Addr, junk_first: bool) -> SocketAddr {
        let server = tokio::net::UdpSocket::bind((Ipv4Addr::LOCALHOST, 0))
            .await
            .unwrap();
        let replier = tokio::net::UdpSocket::bind((Ipv4Addr::LOCALHOST, 0))
            .await
            .unwrap();
        let name_server = server.local_addr().unwrap();
        tokio::spawn(async move {
            let mut buf = [0u8; 512];
            let (len, client) = server.recv_from(&mut buf).await.unwrap();
            let request = Message::from_vec(&buf[..len]).unwrap();
            let mut response = Message::new();
            response
                .set_id(request.id())
                .set_message_type(MessageType::Response);
            for query in request.queries() {
                response.add_query(query.clone());
                response.add_answer(Record::from_rdata(
                    query.name().clone(),
                    60,
                    RData::A(A(ip)),
                ));
            }
            if junk_first {
                replier.send_to(b"not a dns message", client).await.unwrap();
            }
            replier
                .send_to(&response.to_vec().unwrap(), client)
                .await
                .unwrap();
        });
        name_server
    }

    #[tokio::test]
    async fn test_query_a_accepts_reply_from_another_address_than_the_name_server() {
        let ip = Ipv4Addr::new(192, 168, 1, 23);
        let name_server = spawn_responder(ip, false).await;
        let resolved = query_a(name_server, "cvend-1234.local", Duration::from_secs(5))
            .await
            .unwrap();
        assert_eq!(resolved, ip);
    }

    /// Guards against peeking instead of receiving: the junk datagram must be
    /// consumed so that the real reply behind it can be read.
    #[tokio::test]
    async fn test_query_a_skips_junk_datagram_before_the_reply() {
        let ip = Ipv4Addr::new(192, 168, 1, 23);
        let name_server = spawn_responder(ip, true).await;
        let resolved = query_a(name_server, "cvend-1234.local", Duration::from_secs(5))
            .await
            .unwrap();
        assert_eq!(resolved, ip);
    }

    /// Without an answer the query must fail after the timeout
    #[tokio::test]
    async fn test_query_a_fails_after_timeout_without_reply() {
        let silent = tokio::net::UdpSocket::bind((Ipv4Addr::LOCALHOST, 0))
            .await
            .unwrap();
        let name_server = silent.local_addr().unwrap();
        let resolved = query_a(name_server, "cvend-1234.local", Duration::from_millis(100)).await;
        assert!(resolved.is_err());
    }
}
