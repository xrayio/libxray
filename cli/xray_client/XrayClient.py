import sys
import os
import warnings
# supress  UserWarning: Could not load the default wrapper for your platform: cpy, performance may be affected!
warnings.simplefilter("ignore")
import nanomsg
import json

class MsgToXnode(object):
    REQ_ID = 0

    def __init__(self, xquery):
        self.xquery = xquery

    def to_json(self, ts=0):
        self.REQ_ID += 1
        return json.dumps({"req_id": str(self.REQ_ID),
                "widget_id": "xraycli",
                "query": self.xquery,
                "timestamp": int(ts)})

class XrayClient(object):
    XRAY_NODES_PATH = '/tmp/xray/'

    def __init__(self, node):
        self.request = None
        self.node = node
        self.init_socket()

    def init_socket(self):
        self.request = nanomsg.Socket(nanomsg.REQ)
        # self.request.setsockopt(zmq.IDENTITY, "xraycli-" + str(os.getpid()))
        self.request.recv_timeout = 1000
        self.request.send_timeout = 1000
        self.request.connect('ipc://{}/{}'.format(self.XRAY_NODES_PATH, self.node))
        self.intiated = True
    
    def close_socket(self):
        self.request.close()
    
    def send_recv(self, path, fmt):
        if self.intiated is False:
            self.init_socket()
        msg = MsgToXnode(path)
        # print(">SEND ", node, ": ", msg.to_json())
        # self.request.send(node.encode('ascii'), zmq.SNDMORE)
        self.request.send(msg.to_json())
        try:
            msg = self.request.recv()
        except:
            self.close_socket()
            raise
        msg = json.loads(msg)
        # print("<RECV", msg)
        if fmt == 'json':
            return msg
        else:
            return msg["result_set"]
