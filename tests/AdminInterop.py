"""Independent CPython/OpenSSL peer against the production native TLS dispatcher."""
import hashlib, json, queue, socket, ssl, struct, subprocess, sys, threading, time, uuid, warnings
from pathlib import Path
checks = 0
def check(value, message):
    global checks
    checks += 1
    if not value: raise AssertionError(message)
def receive(stream, count):
    result = b""
    while len(result) < count:
        part = stream.recv(count - len(result))
        if not part: raise EOFError("server closed")
        result += part
    return result
def rpc(stream, operation, payload=None, fragmented=False):
    request_id = uuid.uuid4().hex
    data = json.dumps({"version":1,"requestId":request_id,"operation":operation,"payload":payload or {}}).encode()
    wire = struct.pack("!I",len(data)) + data
    if fragmented:
        for at in range(0,len(wire),3): stream.sendall(wire[at:at+3])
    else: stream.sendall(wire)
    size, = struct.unpack("!I",receive(stream,4))
    check(0 < size <= 1048576,"response bound")
    reply = json.loads(receive(stream,size))
    check(reply["requestId"] == request_id,"correlated response")
    return reply
def main():
    started = time.monotonic()
    exe = str(Path(sys.argv[1]).resolve())
    proc = subprocess.Popen([exe],cwd=Path(exe).parent,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                            text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(proc.stdout.readline()),daemon=True).start()
        ready = json.loads(lines.get(timeout=15))
        host, port = ready["endpoint"].split(":")
        def connect(version):
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            context.minimum_version = context.maximum_version = version
            if version == ssl.TLSVersion.TLSv1_1: context.set_ciphers("ALL:@SECLEVEL=0")
            raw = socket.create_connection((host,int(port)),timeout=3)
            try: stream = context.wrap_socket(raw,server_hostname=host)
            except: raw.close(); raise
            # Explicit leaf pin validation BEFORE credentials, independent of the native implementation.
            check(hashlib.sha256(stream.getpeercert(binary_form=True)).hexdigest()==ready["fingerprint"],"server pin")
            return stream
        for version in (ssl.TLSVersion.TLSv1_2,ssl.TLSVersion.TLSv1_3):
            with connect(version) as stream:
                check(rpc(stream,"server.status")["error"]["code"]=="unauthorized","unauthenticated status")
                check(rpc(stream,"hello",fragmented=True)["ok"],"fragmented JSON frame")
                check(rpc(stream,"authenticate",{"password":"Fixture-password-only-2026"})["ok"],"independent authentication")
                check(rpc(stream,"server.status")["payload"]["ready"],"independent status")
                check(rpc(stream,"mods.list")["payload"]==[],"independent mods inventory")
                check(rpc(stream,"logout")["ok"],"logout")
                print("PASS independent",stream.version(),flush=True)
        def rejected_frame(data):
            with connect(ssl.TLSVersion.TLSv1_3) as stream:
                stream.sendall(data)
                try: response=stream.recv(4)
                except (ssl.SSLError,ConnectionError): response=b""
                check(not response,"malformed frame was not disconnected")
        rejected_frame(struct.pack("!I",65537))
        data=b'{"version":1,"version":1}'
        rejected_frame(struct.pack("!I",len(data))+data)
        data=('['*20+'0'+']'*20).encode()
        rejected_frame(struct.pack("!I",len(data))+data)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore",DeprecationWarning)
            try: old=connect(ssl.TLSVersion.TLSv1_1)
            except (ssl.SSLError,ConnectionError): check(True,"TLS 1.1 rejected")
            else: old.close(); raise AssertionError("TLS 1.1 accepted")
        stream=connect(ssl.TLSVersion.TLSv1_3);stream.sendall(struct.pack("!I",100)+b'{');stream.close()
        with connect(ssl.TLSVersion.TLSv1_3) as stream:
            check(rpc(stream,"hello")["ok"],"service recovered after truncated request")
        idle=connect(ssl.TLSVersion.TLSv1_3)
        stop=time.monotonic();proc.stdin.write("stop\n");proc.stdin.flush()
        check(proc.wait(timeout=3)==0,"server shutdown")
        check(time.monotonic()-stop<2,"idle socket cancellation")
        idle.close()
        print(f"PASS {checks} interop/adversarial checks in {(time.monotonic()-started)*1000:.1f} ms")
    finally:
        if proc.poll() is None:
            proc.stdin.write("stop\n");proc.stdin.flush()
            try: proc.wait(timeout=4)
            except subprocess.TimeoutExpired: proc.kill();proc.wait()
        error=proc.stderr.read()
        if error: print(error,file=sys.stderr)
if __name__=="__main__": main()
