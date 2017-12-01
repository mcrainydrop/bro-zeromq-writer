// See the file "COPYING" for copyright.
//
// Log writer for writing to ZeroMQ

#include <cstring>
#include <cstdlib>
#include <zmq.h>

#include "ZeroMQ.h"
#include "zeromqwriter.bif.h"

using namespace logging;
using namespace writer;

ZeroMQ::ZeroMQ(WriterFrontend* frontend): WriterBackend(frontend), formatter(0), zmq_context(0), zmq_publisher(0)
{
    zmq_hostname.assign(
        (const char*) BifConst::LogZeroMQ::zmq_hostname->Bytes(),
        BifConst::LogZeroMQ::zmq_hostname->Len());
    zmq_port = BifConst::LogZeroMQ::zmq_port;

    // Create zmq context (this is shared by all threads)
    zmq_context = zmq_ctx_new();
}

ZeroMQ::~ZeroMQ()
{
    zmq_ctx_destroy(zmq_context);
}

string ZeroMQ::GetConfigValue(const WriterInfo& info, const string name) const
{
    map<const char*, const char*>::const_iterator it = info.config.find(name.c_str());
    if (it == info.config.end())
        return string();
    else
        return it->second;
}

bool ZeroMQ::DoInit(const WriterInfo& info, int num_fields, const threading::Field* const* fields)
{
    string hostname = zmq_hostname;
    string cfg_hostname = GetConfigValue(info, "hostname");

    if (!cfg_hostname.empty())
        hostname = cfg_hostname;

    int port = zmq_port;
    string cfg_port = GetConfigValue(info, "port");

    if (!cfg_port.empty())
        port = strtoul(cfg_port.c_str(), nullptr, 10);

    // The log path name (e.g. "conn") will be sent for each log message
    log_path = info.path;

    // Initialize the formatter
    formatter = new threading::formatter::JSON(this, threading::formatter::JSON::TS_EPOCH);

    // Create zmq socket
    zmq_publisher = zmq_socket(zmq_context, ZMQ_PUB);
    if (!zmq_publisher) {
        Error(Fmt("Failed to create zmq socket for log path '%s': %s", log_path, strerror(errno)));
        return false;
    }

    // Set the LINGER time to prevent "broctl stop" from hanging when there
    // are unsent log messages and a connection to a subscriber is interrupted.
    int millisecs = 0;
    zmq_setsockopt(zmq_publisher, ZMQ_LINGER, &millisecs, sizeof(int));

    // Connect to the zmq subscriber
    int rc = zmq_connect(zmq_publisher, Fmt("tcp://%s:%d", hostname.c_str(), port));
    if (rc) {
        Error(Fmt("Failed to connect zmq socket for log path '%s': %s", log_path, strerror(errno)));
        return false;
    }

    return true;
}

bool ZeroMQ::DoFinish(double network_time)
{
    zmq_close(zmq_publisher);
    delete formatter;

    return true;
}

bool ZeroMQ::DoWrite(int num_fields, const threading::Field* const* fields, threading::Value** vals)
{
    ODesc buffer;
    buffer.Clear();

    formatter->Describe(&buffer, num_fields, fields, vals);

    const char* msg = (const char*)buffer.Bytes();

    int nbytes = zmq_send(zmq_publisher, log_path, strlen(log_path), ZMQ_SNDMORE);
    if (nbytes == -1) {
        Error(Fmt("Failed to send topic message '%s' to ZeroMQ: %s", log_path, strerror(errno)));
    }

    nbytes = zmq_send(zmq_publisher, msg, strlen(msg), 0);
    if (nbytes == -1) {
        Error(Fmt("Failed to send '%s' log message to ZeroMQ: %s", log_path, strerror(errno)));
    }

    return true;
}

bool ZeroMQ::DoSetBuf(bool enabled)
{
    return true;
}

bool ZeroMQ::DoFlush(double network_time)
{
    return true;
}

bool ZeroMQ::DoRotate(const char* rotated_path, double open, double close, bool terminating)
{
    // No log rotation needed
    return FinishedRotation();
}

bool ZeroMQ::DoHeartbeat(double network_time, double current_time)
{
    return true;
}
