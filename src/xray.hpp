/*
 * xray.hpp
 *
 *  Created on: 25 Aug 2017
 *      Author: gregory
 */

#ifndef SRC_XRAY_HPP_
#define SRC_XRAY_HPP_

#include <string>
#include <memory>
#include <thread>
#include <list>

#include <zmq.h>
#include <zmq.hpp>

#include "xray.h"

using namespace std;

typedef vector<vector<string>> ResultSet;
class XPathNode;
class XType;

class XNode {
	shared_ptr<XPathNode> root_xpath = make_shared<XPathNode>("/");
	public:
		list<weak_ptr<XPathNode>> expire_pnodes;
		unordered_map <string, shared_ptr<XType> > types;

		XNode();
		void xadd(void *xobj,
				  int n_rows,
				  const string &xpath_str,
				  const string &xtype_str,
				  xray_iterator iterator_cb=nullptr);
		void xdelete(const string &xpath_str);
		shared_ptr<ResultSet> xdump(const string &xtype_str);
		void destroy();
		void register_basic_types();
		void register_type(shared_ptr<XType> xtype);
		shared_ptr<XPathNode> find_path_node(const string &xpath_str,
											 bool create_path);
};

class XClient {
	zmq::context_t *ctx = nullptr;
	zmq::socket_t *xray_server_socket = nullptr;
	zmq::socket_t *xray_cli_socket = nullptr;

	string node_id;
	string xray_server_conn;
	string xray_cli_conn;

	bool debug = false;
	unordered_map<string, string> cfg;
	uint32_t time;
	uint32_t expire_ts;

	void destroy_socket();

	zmq::socket_t *rx_socket(zmq::message_t &request);

	zmq::socket_t *rx(string &rs, string &req_id, uint64_t &ts, string &widget_id);

	void _tx(zmq::socket_t *socket,
			 ResultSet &msg,
			 const string &req_id, uint64_t ts,
			 int avg_ms,
			 const string &widget_id="");

	void tx(zmq::socket_t *socket,
			ResultSet &rs,
			const string &req_id="",
			uint64_t ts=0,
			int avg_ms=0,
			const string &widget_id="");

	void tx(zmq::socket_t *socket,
			string &msg,
			const string &req_id="",
			uint64_t ts=0,
			int avg_ms=0);

	void expire_captures();
	void _start();
	void get_cfg(const string &api_key);

public:
	thread *xclient_thread = nullptr;
	mutex back_end_lock; // Lock btw back end / front end

	/* for tests */
	int rx_timeout = 30000;

	XNode xnode;

	XClient(const string &api_key);
	~XClient();
	void init_socket();
	// TODO: implement thread stop
	shared_ptr<ResultSet> handle_query(const string &query);
	void start();
};



#endif /* SRC_XRAY_HPP_ */
