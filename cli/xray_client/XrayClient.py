import sys
import os
import warnings
# supress  UserWarning: Could not load the default wrapper for your platform: cpy, performance may be affected!
warnings.simplefilter("ignore")
import nanomsg
import json
import functools

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

    def get_xray_stats(func):
        @functools.wraps(func)
        def wrapper(self, query_path, *args, **kwargs):
            fmt = args[0] if args else None
            result_set = func(self, query_path, fmt)

            # in case of fmt is json need only to arrange the result set list of lists
            header = result_set["result_set"][0] if fmt else result_set[0]
            res_set = result_set["result_set"][1:] if fmt else result_set[1:]
            result = list()
            for entry in res_set:
                row = dict()
                for index, col in enumerate(header):
                    row[col] = entry[index]

                if kwargs:
                    for key in kwargs:
                        if row[key] == kwargs[key]:
                            result.append(row)
                else:
                    result.append(row)
            if fmt:
                result_set["result_set"] = result
                return result_set
            else:
                return result

        return wrapper

    @get_xray_stats
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

