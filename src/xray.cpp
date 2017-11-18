//============================================================================
// Name        : xray.cpp
// Author      :
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
// g++ -g -O0 -std=c++14 -o libxray libxraycpp.cpp
//============================================================================
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <cstdarg>
#include <thread>

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>

#include "ordered_map.h"
#define ZMQ_CPP11
#include "zmq.h"
#include "zmq.hpp"
#include "json.hpp"

#include "xray.hpp"
#include "xray.h" 		/* c api */

using namespace std;
using json = nlohmann::json;

class XPathNode;
class XType;
class XSlot;

unordered_map <string, shared_ptr<XType> > types;
typedef pair<vector<char>, uint32_t> capture_t;

/**
 * Static functions, HELPERS
 */

#define BREAKPOINT	__asm__("int $3")

#define EXPIRE_CAPTURE_CHECK_SEC (10)
#define EXPIRE_CAPTURE_SEC  	 (4)

template<class T>
class Stats {
	static int instance_count;
public:
	Stats() {
		instance_count++;
		this->print();
	}
	~Stats() {
		instance_count--;
		this->print();
	}
	static void print() {
		std::cout << instance_count << " instances of " << typeid(T).name()
				<< ", " << sizeof(T) << " bytes each." << std::endl;
	}
};

template<class T>
int Stats<T>::instance_count = 0;

string string_vsprintf(const char* format, va_list args) {
    va_list tmp_args; //unfortunately you cannot consume a va_list twice
    va_copy(tmp_args, args); //so we have to copy it
    const int required_len = vsnprintf(nullptr, 0, format, tmp_args) + 1;
    va_end(tmp_args);
    string buf(required_len, '\0');
    if (vsnprintf(&buf[0], buf.size(), format, args) < 0) {
        throw runtime_error{"string_vsprintf encoding error"};
    }
    buf.resize(buf.size() - 1);
    return buf;
}

string string_sprintf(const char* format, ...) __attribute__ ((format (printf, 1, 2)));

string string_sprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    string str{string_vsprintf(format, args)};
    va_end(args);
    return str;
}

static vector<string> split(const string &text, char sep) {
  vector<string> tokens;
  size_t start = 0, end = 0;
  while ((end = text.find(sep, start)) != string::npos) {
    tokens.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  tokens.push_back(text.substr(start));
  return tokens;
}

static vector<string> xpath_to_seg(const string &path) {
    if(path == "/") {
        return {};
    }

    vector<string> xpath_segs;
    for(auto xpath_seg : split(path, '/')) {
        if(xpath_seg == "")
            continue;
        xpath_segs.push_back(xpath_seg);
    }
    if(xpath_segs.size() == 0)
        throw invalid_argument("not valid path");
    return xpath_segs;
}

class range {
 public:
   class iterator {
      friend class range;
    public:
      long int operator *() const { return i_; }
      const iterator &operator ++() { ++i_; return *this; }
      iterator operator ++(int) { iterator copy(*this); ++i_; return copy; }

      bool operator ==(const iterator &other) const { return i_ == other.i_; }
      bool operator !=(const iterator &other) const { return i_ != other.i_; }

    protected:
      iterator(long int start) : i_ (start) { }

    private:
      unsigned long i_;
   };

   iterator begin() const { return begin_; }
   iterator end() const { return end_; }
   range(long int  begin, long int end) : begin_(begin), end_(end) {}
private:
   iterator begin_;
   iterator end_;
};

static int64_t get_int_value_of_slot(int slot_size, void *slot_ptr, int is_reference) {
	int64_t ret = 0;
	if(is_reference)
		return (int64_t)slot_ptr;
	memcpy(&ret, slot_ptr, slot_size);
	return ret;
}

static void add_int_value_of_slot(int slot_size, int is_signed, void *slot_dst_ptr, void *slot_toadd_ptr, int is_refernce) {
	switch(slot_size) {
		case 1:
			if(is_signed)
				*(int8_t *)slot_dst_ptr += *(int8_t *)slot_toadd_ptr;
			else
				*(uint8_t *)slot_dst_ptr += *(uint8_t *)slot_toadd_ptr;
			break;
		case 2:
			if(is_signed)
				*(int16_t *)slot_dst_ptr += *(int16_t *)slot_toadd_ptr;
			else
				*(uint16_t *)slot_dst_ptr += *(uint16_t *)slot_toadd_ptr;
			break;
		case 4:
			if(is_signed)
				*(int32_t *)slot_dst_ptr += *(int32_t *)slot_toadd_ptr;
			else
				*(uint32_t *)slot_dst_ptr += *(uint32_t *)slot_toadd_ptr;
			break;
		case 8:
			if(is_signed)
				*(int64_t *)slot_dst_ptr += *(int64_t *)slot_toadd_ptr;
			else
				*(uint64_t *)slot_dst_ptr += *(uint64_t *)slot_toadd_ptr;
			break;
	}
}

/**
 * EXECPTIONS
 */

class cluster_not_exists_err: public std::exception
{
	const char* what() const throw() { return "Cluster not exists\n"; }
};

class quit_err: public std::exception
{
	const char* what() const throw() { return "Recved quit\n"; }
};

class msg_rx_timeout_err: public std::exception
{
	const char* what() const throw() { return "Recved quit\n"; }
};

class xpath_not_exists_err: public std::exception
{
	const char* what() const throw() { return "xpath not exists\n"; }
};

class fmt_cb_result_too_big: public std::exception
{
	const char* what() const throw () {

		return "fmt_db_result_too_big \n";
	}
};


/**
 * VXSLOT
 */

class VSlot{
	public:
		string name;
		xray_vslot_fmt_cb fmt_cb;
		xray_vslot_args_t args;
		bool is_args = false;
	public:
		VSlot(const string &name, xray_vslot_fmt_cb fmt_cb, xray_vslot_args_t *vslot_args);
};

VSlot::VSlot(const string &name, xray_vslot_fmt_cb fmt_cb, xray_vslot_args_t *vslot_args) {
	this->name = name;
	this->fmt_cb = fmt_cb;
	if(vslot_args) {
		this->args = *vslot_args;
		this->is_args = true;
	}
}


/**
 * XSLOT
 */

class XSlot {
public:
	string name;
	int offset;
	bool is_pointer;
	bool is_refernce;
	int arr_size;
	shared_ptr<XType> type;
	int flags;

	XSlot(const string &name, int offset, bool is_pointer, bool is_reference,
			int arr_size, shared_ptr<XType> type, int flags);
};

XSlot::XSlot(const string &name,
		 	 int offset,
			 bool is_pointer,
			 bool is_reference,
			 int arr_size,
			 shared_ptr<XType> type,
			 int flags)
{
	this->name = name;
	this->offset = offset;
	this->is_pointer = is_pointer;
	this->is_refernce = is_reference;
	this->arr_size = arr_size;
	this->type = type;
	this->flags = flags;
}

static void rs_from_json(ResultSet& rs, const json& j) {
	for(auto elm : j) {
		vector<string> row;
		for(auto str : elm) {
			row.push_back(str);
		}
		rs.push_back(row);
	}
}

static void print_rs(ResultSet &rs) {
	for(auto row : rs) {
		for(auto &slot : row) {
			cout << slot << " ";
		}
		cout << endl;
	}
	cout.flush();
}

static void assert_rs(ResultSet &rs, ResultSet &rs_expected) {
	assert(rs.size() == rs_expected.size());
	for(auto row_id : range(0, rs.size())) {
		assert(rs[row_id].size() == rs_expected[row_id].size());
		for(auto slot_id : range(0, rs[row_id].size())) {
			assert(rs[row_id][slot_id].size() == rs_expected[row_id][slot_id].size());
			assert(rs[row_id][slot_id] == rs_expected[row_id][slot_id]);
		}
	}
}

/**
 * XTYPE
 */

class XType {
	public:
		int size = 0;
		string fmt;
		xray_fmt_type_cb fmt_cb;
		tsl::ordered_map<string, shared_ptr<XSlot>> slots;
		tsl::ordered_map<string, shared_ptr<VSlot>> vslots;
		string name;
		shared_ptr<XSlot> pk = nullptr;
		bool capture_needed = false;


		XType(const string &name, int size, const string &fmt="", xray_fmt_type_cb fmt_cb=nullptr);
		void add_slot(const string &name, int offset, int size, const string &type, bool is_pointer=false, int array_size=0, int flags=0);
		void add_vslot(const string &name, xray_vslot_fmt_cb fmt_cb, xray_vslot_args_t *args);
};

XType::XType(const string &name, int size, const string &fmt, xray_fmt_type_cb fmt_cb) {
	this->name = name;
	this->size = size;
	this->fmt = fmt;
	this->fmt_cb = fmt_cb;
}

static uint32_t get_current_timestamp() {
	struct timespec tm;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tm);

	uint64_t miliseconds = tm.tv_sec * 1000 + tm.tv_nsec / 1000 / 1000;
	return miliseconds;
}

static uint32_t timestamp_diff_in_sec(uint32_t ts_a, uint32_t ts_b) {
	return (ts_a - ts_b) / 1000;
}

int64_t calc_slot_rate(void *row, xray_vslot_args_t *args) {
	uint64_t curr = 0;
	uint64_t captured = 0;

	shared_ptr<XSlot> slot = *static_cast<shared_ptr<XSlot> *>(args->slot);

	void *slot_ptr = (char *)row + slot->offset;
	void *captured_slot = (char *)args->data + slot->offset;

	uint32_t curr_ts = get_current_timestamp();
	uint32_t diff_ts = curr_ts - args->timestamp;
	memcpy(&curr, slot_ptr, slot->type->size);
	memcpy(&captured, captured_slot, slot->type->size);

	uint64_t diff = curr - captured;
	if(diff_ts == 0)
		return 0;
	return diff * 1000 /*miliseconds*/ / diff_ts ;
}

static bool is_complex_slot(shared_ptr<XSlot> slot) {
	return slot->type->slots.size() > 0;
}

void XType::add_slot(const string &name,
		  	  	  	 int offset,
					 int size,
					 const string &slot_type_str,
					 bool is_pointer,
					 int array_size,
					 int flags)
{
	auto found_slot_type = types.find(slot_type_str);
	if(found_slot_type == types.end())
		throw invalid_argument("cannot find type '" + slot_type_str + "'");
	if(size != found_slot_type->second->size && found_slot_type->second->fmt_cb == NULL)
		throw invalid_argument("size of slot, isn't same as of xtype.");

	slots[name] = make_shared<XSlot>(name, offset, is_pointer, false, 0, found_slot_type->second, flags);

	if(flags & XRAY_FLAG_PK) {
		if(is_complex_slot(slots[name]))
			throw invalid_argument("cannot set pk on complex slot");

		pk = slots[name];
	}

	if(flags & XRAY_FLAG_RATE) {
		capture_needed = true;

		string rate_name = name + "-" + "rate";
		xray_vslot_args_t args = {0};
		args.slot = (void *)&slots[name];

		add_vslot(rate_name, calc_slot_rate, &args);
	}

}

void XType::add_vslot(const string &name, xray_vslot_fmt_cb fmt_cb, xray_vslot_args_t *args) {
	vslots[name] = make_shared<VSlot>(name, fmt_cb, args);
}

static void xdump_slots(vector<string> &header_raw,
						tsl::ordered_map<string, shared_ptr<XSlot>> &slots,
						tsl::ordered_map<string, shared_ptr<VSlot>> &vslots) {
	for(auto slot_entry : slots) {
		shared_ptr<XSlot> slot = slot_entry.second;
		if(is_complex_slot(slot)) {
			xdump_slots(header_raw, slot->type->slots, slot->type->vslots);
			continue;
		}
		header_raw.push_back(slot_entry.first);
	}
	for(auto slot_entry : vslots) {
		header_raw.push_back(slot_entry.first);
	}
}

/**
 * XPathNode
 */
class XPathNode {
	public:
		void *xobj = nullptr;
		tsl::ordered_map <string, shared_ptr<XPathNode>> sons;
		shared_ptr<XType> xtype = nullptr;
		XNode *xnode;
		weak_ptr<XPathNode> self;

		uint32_t last_capture_ts = 0;

		/* user config */
		xray_iterator iterator_cb = nullptr;
		int n_rows = 0;

		XPathNode(const string &xpath_str,
				  int n_rows,
				  XNode *xnode = nullptr);

		vector<string> dump_son_names();
		shared_ptr<ResultSet> xdump();
		void xdump_dirs(shared_ptr<ResultSet> &rs, const string path="/");
		void clear_captures();

	private:
		/* rate calculation */
		unordered_map<string, capture_t> captures;


		string name;
		weak_ptr<XPathNode> father_node;

		XPathNode& parse_path_str(const string &xpath_str);
		void xdump_xobj(shared_ptr<ResultSet> &rs);
		bool format_row(vector<string> &row, void *row_ptr, capture_t *capture, shared_ptr<XType> xtype);
		string format_slot(shared_ptr<XSlot> &slot, void *slot_ptr);
		void handle_row(shared_ptr<ResultSet> &rs, void *row_ptr);
};

void XPathNode::clear_captures() {
	last_capture_ts = 0;
	captures.clear();
}

string XPathNode::format_slot(shared_ptr<XSlot> &slot, void *slot_ptr) {
	int slot_size = slot->type->size;
	string formatted(XRAY_MAX_SLOT_STR_SIZE * 2, 0);
	if(slot->type->fmt_cb) {
		int len = slot->type->fmt_cb(slot_ptr, &formatted[0]);
		formatted.resize(len);
		if(len > XRAY_MAX_SLOT_STR_SIZE)
			throw fmt_cb_result_too_big();
	} else {
		uintptr_t slot_uintptr = get_int_value_of_slot(slot_size, slot_ptr, slot->is_refernce);
		formatted = string_sprintf(slot->type->fmt.c_str(),  slot_uintptr);
	}
	return move(formatted);
}

bool XPathNode::format_row(vector<string> &row, void *row_ptr, capture_t *cap, shared_ptr<XType> xtype) {
	bool is_empty_row = true;

	for(auto &slot_entry: xtype->slots) {
		shared_ptr<XSlot> slot = slot_entry.second;
		void *slot_ptr = (uint8_t *)row_ptr + slot->offset;

		if(is_complex_slot(slot)) {
			is_empty_row &= format_row(row, slot_ptr, nullptr, slot->type);
			continue;
		}

		string formatted = format_slot(slot, slot_ptr);
		if(formatted != "0" and !(slot->flags & XRAY_FLAG_CONST)) {
			is_empty_row = false;
		}
		row.push_back(formatted);
	}

	for(auto &iterator: xtype->vslots) {
		void *slot_capture = nullptr;
		xray_vslot_args_t *args = nullptr;
		auto vslot = iterator.second;

		if(vslot->is_args && cap)
		{
			args = &vslot->args;
			args->data = cap->first.data();
			args->timestamp = cap->second;
		}

		int64_t vslot_out = vslot->fmt_cb(row_ptr, args);
		string formatted = string_sprintf("%" PRIi64,  vslot_out);
		if(formatted != "0")
			is_empty_row = false;
		row.push_back(formatted);
	}

	return is_empty_row;
}

void XPathNode::handle_row(shared_ptr<ResultSet> &rs, void *row_ptr) {
	auto row = vector<string>{};
	capture_t *cap = nullptr;
	string pk;

	if(xtype->capture_needed) {
		void *slot_ptr = (char *)row_ptr + xtype->pk->offset;
		string pk = format_slot(xtype->pk, slot_ptr);
		cap = &captures[pk];
		if(cap->second == 0) {
			cap->first.resize(xtype->size);
			memcpy(cap->first.data(), row_ptr, xtype->size);
			cap->second = get_current_timestamp();
		}
	}

	if(format_row(row, row_ptr, cap, xtype) == false)
		rs->push_back(move(row));

	if(xtype->capture_needed) {
		memcpy(cap->first.data(), row_ptr, xtype->size);
		cap->second = get_current_timestamp();
	}

}

void XPathNode::xdump_xobj(shared_ptr<ResultSet> &rs)
{
	if(xtype->capture_needed) {
		if(last_capture_ts == 0) {
			xnode->expire_pnodes.push_back(self);
		}
		last_capture_ts = get_current_timestamp();

	}

	if(iterator_cb) {
		uint8_t state[XRAY_STATE_MAX_SIZE] = {0};
		char mem[xtype->size];

		void *row_ptr = iterator_cb(xobj, state, mem);
		while(row_ptr != nullptr)
		{
			handle_row(rs, row_ptr);
			row_ptr = iterator_cb(xobj, state, mem);
		}
	}

	int row_offset = 0;
	for(auto idx : range(0, n_rows)) {
		void *row_ptr = (uint8_t *)xobj + row_offset;
		handle_row(rs, row_ptr);
		row_offset += (xtype->size);
	}
}

void XPathNode::xdump_dirs(shared_ptr<ResultSet> &rs, const string path) {
	for(auto son : sons) {
		if(son.second->xobj)
			rs->push_back(vector<string>{path + son.second->name});
		else
			son.second->xdump_dirs(rs, path + son.second->name + "/");
	}
}

shared_ptr<ResultSet> XPathNode::xdump() {
	shared_ptr<ResultSet> rs = make_shared<ResultSet>();
	if(xobj) {
		rs->push_back(vector<string>{});
		xdump_slots(rs->front(), xtype->slots, xtype->vslots);
		xdump_xobj(rs);
	} else {
		rs->push_back(vector<string>{"dir"});
		xdump_dirs(rs);
	}
	return rs;
}

XPathNode::XPathNode(const string &xpath_str,
					 int n_rows,
					 XNode *xnode) {
	this->name = xpath_str;
	this->n_rows = n_rows;
	this->xnode = xnode;
}

static void register_type(shared_ptr<XType> xtype) {
	auto found = types.find(xtype->name);
	if(found != types.end())
		throw invalid_argument("cannot register type, type already exists:'" + xtype->name + "'");
	types[xtype->name] = xtype;
}


/**
 * XOBJ
 */
class XObj {

};


/**
 * XNODE
 */

void XNode::destroy() {
	root_xpath = nullptr;
	xobj_to_path.clear();
}

shared_ptr<ResultSet> XNode::xdump(const string &xpath) {
    auto father_xpnode = root_xpath;
    for(auto &xpath_seg : xpath_to_seg(xpath)) {
        auto xpath_node = father_xpnode->sons.find(xpath_seg);
        if(xpath_node == father_xpnode->sons.end())
            throw xpath_not_exists_err();
        father_xpnode = xpath_node->second;
    }
    return father_xpnode->xdump();
}

//TODO: if xclient started, cannot add or lock and add
void XNode::xadd(void *xobj, int n_rows, const string &xpath_str, const string &xtype_str, xray_iterator iterator_cb) {
	auto xtype = types.find(xtype_str);
	if(xobj == nullptr)
		throw invalid_argument("cannot add xobj, xobj must not be null");
	if(n_rows && iterator_cb)
		throw invalid_argument("cannot set both n_rows and iterator_cb, use one of them");
	if(xtype == types.end())
		throw invalid_argument("cannot add xobj, type not exists:'" + xtype_str + "'");

	bool is_cap_needed = false;
	bool is_pk_exists = false;
	for(auto &slot_it : xtype->second->slots) {
		is_cap_needed |= slot_it.second->flags & XRAY_FLAG_RATE;
		is_pk_exists |= slot_it.second->flags & XRAY_FLAG_PK;
	}

	if(is_cap_needed && !is_pk_exists)
		throw invalid_argument("cannot add xobj, type '" + xtype_str + "' has rate enabled but no PK");

	auto father_xpnode = root_xpath;
	for(auto &xpath_seg : xpath_to_seg(xpath_str)) {
		if(xpath_seg == "")
			continue;
		auto xpath_node_res = father_xpnode->sons.find(xpath_seg);
		if(xpath_node_res == father_xpnode->sons.end()) {
			auto xpnode = make_shared<XPathNode>(xpath_seg, n_rows, this);
			father_xpnode->sons[xpath_seg] = xpnode;
			xpnode->self = xpnode;
			father_xpnode = xpnode;
		} else {
			father_xpnode = xpath_node_res->second;
		}
	}
	father_xpnode->xobj = xobj;
	father_xpnode->xtype = xtype->second;
	father_xpnode->n_rows = n_rows;
	father_xpnode->iterator_cb = iterator_cb;
	xobj_to_path[xobj] = xpath_str;
}

static int xray_string_fmt(void *slot, char *output_str) {
	return snprintf(output_str, XRAY_MAX_SLOT_STR_SIZE, "%s", (char *)slot);
}

static int xray_p_string_fmt(void *slot, char *output_str) {
	c_p_string_t str = (c_p_string_t)slot;
	return snprintf(output_str, XRAY_MAX_SLOT_STR_SIZE, "%s", *str);
}

static void register_basic_types(void) {
	register_type(make_shared<XType>("uint8_t", sizeof(uint8_t), "%hhu"));
	register_type(make_shared<XType>("uint16_t", sizeof(uint16_t), "%hu"));
	register_type(make_shared<XType>("uint32_t", sizeof(uint32_t), "%u"));
	register_type(make_shared<XType>("uint64_t", sizeof(uint64_t), "%llu"));

	register_type(make_shared<XType>("int8_t", sizeof(uint8_t), "%hhd"));
	register_type(make_shared<XType>("int16_t", sizeof(uint16_t), "%hd"));
	register_type(make_shared<XType>("int32_t", sizeof(uint32_t), "%d"));
	register_type(make_shared<XType>("int64_t", sizeof(uint64_t), "%lld"));

	register_type(make_shared<XType>("char", sizeof(char), "%c"));
	register_type(make_shared<XType>("short", sizeof(short), "%hd"));
	register_type(make_shared<XType>("short int", sizeof(short int), "%hd"));
	register_type(make_shared<XType>("int", sizeof(int), "%d"));
	register_type(make_shared<XType>("long", sizeof(long), "%ld"));
	register_type(make_shared<XType>("long int", sizeof(long int), "%ld"));
	register_type(make_shared<XType>("long long", sizeof(long long), "%lld"));
	register_type(make_shared<XType>("long long int", sizeof(long long int), "%lld"));

	register_type(make_shared<XType>("unsigned short", sizeof(short), "%hu"));
	register_type(make_shared<XType>("unsigned short int", sizeof(short int), "%hu"));
	register_type(make_shared<XType>("unsigned int", sizeof(int), "%u"));
	register_type(make_shared<XType>("unsigned long", sizeof(long), "%lu"));
	register_type(make_shared<XType>("unsigned long int", sizeof(long int), "%lu"));
	register_type(make_shared<XType>("unsigned long long", sizeof(long long), "%llu"));
	register_type(make_shared<XType>("unsigned long long int", sizeof(long long int), "%llu"));

	xray_create_type(c_string_t, xray_string_fmt);
	xray_create_type(c_p_string_t, xray_p_string_fmt);
}

static string create_node_id(const string &hostname, const string &nodename,
					  	     const string &node_index, const string &key) {
	string s = {nodename + ":" + node_index + ":" + key + ":" + hostname};
	return s;
}

#define XRAY_CFG_ENV "XRAY_CONFIG"
#define XRAYIO_HOST "www.xrayio.com"
#define XRAYIO_PORT "40001"

#define HELLO_MSG_PREFIX "xnode-ok-"

extern char *__progname;

static void set_if_exists(unordered_map<string, string> &map, const string &value, json &cfg, const string &def)
{
	if (cfg.find( value ) == cfg.end()) {
		map[value] = def;
	} else {
		map[value] = cfg[value];
	}
}

void XClient::get_cfg(const string &api_key) {
	char *env = getenv(XRAY_CFG_ENV);
	string cfg_json;
	if(env == nullptr)
		cfg_json = "{}";
	else
		cfg_json = env;

	/*hostname*/
	char buff[128];

	string hostname;
	if(gethostname(buff, sizeof(buff))) {
		hostname = "no_name";
	}
	hostname = buff;

	auto env_json = json::parse(cfg_json);
	set_if_exists(cfg, "server", env_json, XRAYIO_HOST);
	set_if_exists(cfg, "port", env_json, XRAYIO_PORT);
	set_if_exists(cfg, "api_key", env_json, api_key);
	set_if_exists(cfg, "hostname", env_json, hostname);
	set_if_exists(cfg, "debug", env_json, "false");

	json effective_json(cfg);

	cout << XRAY_CFG_ENV << ": " << env_json.dump() << endl;
	cout << "effective config: " << effective_json.dump() << endl;

}

XClient::XClient(const string &api_key) {
	get_cfg(api_key);
	node_id = create_node_id(cfg["hostname"], __progname, cfg["hostname"], cfg["api_key"]);
	conn = "tcp://" + cfg["server"] + ":" + cfg["port"];
	if(cfg["debug"] == "true")
		debug = true;
}

XClient::~XClient() {
	//TODO: send xnode-exit
	destroy_socket();
}

void XClient::destroy_socket() {
	if(res) {
		res->close();
		delete res;
	}
	//if(ctx) {
	//	ctx->close();
	//	delete ctx;
	//}
}

void XClient::init_socket() {
	destroy_socket();

	if(ctx == nullptr)
		ctx = new zmq::context_t(1);
	res = new zmq::socket_t(*ctx, zmq::socket_type::dealer);

	res->setsockopt(ZMQ_IDENTITY, node_id.c_str(), node_id.size());
	res->setsockopt(ZMQ_RCVTIMEO, rx_timeout);
	res->connect(conn);
	cout << "Connecting to: " << conn << " ..." << endl;
}

void XClient::_tx(ResultSet &rs, const string &req_id, uint64_t ts, int avg_ms, const string &widget_id) {
	json response = json::object();
	json rs_json = rs;

	response["req_id"] = req_id;
	response["result_set"] = rs_json;
	response["timestamp"] = ts;
	response["avg_load_ms"] = avg_ms;
	response["widget_id"] = widget_id;

	string res_str = response.dump(-1, ' ', true);
	if(debug)
		cout << "sending: " << res_str << endl;
	res->send("", 0, ZMQ_SNDMORE);
	res->send(res_str.c_str(), res_str.size());
}


void XClient::tx(ResultSet &rs, const string &req_id, const uint64_t ts, int avg_ms, const string &widget_id) {
	_tx(rs, req_id, ts, avg_ms, widget_id);
}

void XClient::tx(string &msg, const string &req_id, const uint64_t ts, int avg_ms) {
	ResultSet rs = {{msg}};
	_tx(rs, req_id, ts, avg_ms);
}

void XClient::rx(string &query, string &req_id, uint64_t &ts, string &widget_id) {
	zmq::message_t message;
	zmq::message_t message1;
	zmq::message_t message2;

	res->recv(&message, ZMQ_RCVMORE);
	res->recv(&message1, ZMQ_RCVMORE);
	res->recv(&message2);

	string msg_str = string(static_cast<char*>(message2.data()), message2.size());
	if(debug)
		cout << "XClient recv: " << msg_str << "size " << message2.size() << endl;
	if(msg_str == "")
		throw msg_rx_timeout_err();
	//TODO: add here exception handler
	/*
		 @throw parse_error.101 in case of an unexpected token
		@throw parse_error.102 if to_unicode fails or surrogate error
		@throw parse_error.103 if to_unicode fails
		*/
	auto json = json::parse(msg_str);
	//TODO: check json array and size
	req_id = json["req_id"];
	query = json["query"];
	ts = json["timestamp"];
	widget_id = json["widget_id"];
}

inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

shared_ptr<ResultSet> XClient::handle_query(const string &query) {
	if(query == "/..err-no_cluster") {
		cerr << "cluster not exists";
		throw cluster_not_exists_err();
	}
	if(query == "/..quit") {
		throw quit_err();
	}
	if(query == "/..ping") {
		auto rs = make_shared<ResultSet>();
		rs->push_back({"pong"});
		return rs;
	}

	try {
		return xnode.xdump(query);
	} catch (const xpath_not_exists_err &e){
		cout << "xpath_not_exists_err" << endl;
		auto rs = make_shared<ResultSet>();
		rs->push_back(vector<string>{"xquery-invlalid"});
		return rs;
	}
}

void XClient::expire_captures() {
	if(timestamp_diff_in_sec(time, expire_ts) < EXPIRE_CAPTURE_CHECK_SEC)
		return;

	for(auto it = xnode.expire_pnodes.begin(); it != xnode.expire_pnodes.end();) {
		if(auto pnode = it->lock()) {
			if(timestamp_diff_in_sec(time, pnode->last_capture_ts) > EXPIRE_CAPTURE_SEC)
			{
				pnode->clear_captures();
				it = xnode.expire_pnodes.erase(it);
				continue; /* don't increment iterator , elemnt was deleted */
			}

		}
		++it;
	}

	expire_ts = time;
}

void XClient::_start() {
	string hello_msg = HELLO_MSG_PREFIX + node_id;
	while(true) {
		try{
			init_socket();
			tx(hello_msg);
		} catch (const exception &e) {
			cout << "Connect to XRAYIO server failed: " << e.what() << endl;
		}
		bool cont_rx = true;
		while(cont_rx) {
			time = get_current_timestamp();
			string req_id = "";
			uint64_t ts;
			string query;
			string widget_id = "";
			try {
				expire_captures();
				rx(query, req_id, ts, widget_id);
				auto rs = handle_query(query);
				tx(*rs, req_id, ts, 0, widget_id);
			} catch (const quit_err &e) {
				cout << "Bye bye, received quit" << endl;
				cont_rx = false;
			} catch (const cluster_not_exists_err &e) {
				cout << "cluster not exists" << endl;
				return;
			} catch (const msg_rx_timeout_err &e) {
				cout << "rx timeout, reconnecting" << endl;
				cont_rx = false;
			} catch (const exception &e) {
				cout << "unknown error: " << e.what() << endl;
				cont_rx = false;
			}
		}

	}
}

void XClient::start() {
	xclient_thread = new thread([this] { this->_start(); });
}

/**
 * TESTS
 */

#include <stdint.h>

#define slot_metadata(container, slot_name) #slot_name, offsetof(container, slot_name), sizeof(((container *)0)->slot_name)
typedef struct test_basic_fmt {
	uint8_t  a_uint8_t;
	uint16_t a_uint16_t;
	uint32_t a_uint32_t;
	uint64_t a_uint64_t;
	int8_t  a_int8_t;
	int16_t a_int16_t;
	int32_t a_int32_t;
	int64_t a_int64_t;
} test_struct_t;

typedef struct test_simple {
	int a;
} test_simple_t;


static void basic_test_fmt() {
	XNode xnode;
	test_struct_t ts = {0xf0, 0xf000, 0xf0000000, 0xf000000000000000,
						(int8_t)0xf0, (int16_t)0xf000,
						(int32_t)0xf0000000, (int64_t)0xf000000000000000};
	shared_ptr<XType> xtype = make_shared<XType>("test_basic_fmt", sizeof(test_basic_fmt));
	xtype->add_slot(slot_metadata(test_struct_t, a_uint8_t), "uint8_t");
	xtype->add_slot(slot_metadata(test_struct_t, a_uint16_t), "uint16_t");
	xtype->add_slot(slot_metadata(test_struct_t, a_uint32_t), "uint32_t");
	xtype->add_slot(slot_metadata(test_struct_t, a_uint64_t), "uint64_t");
	xtype->add_slot(slot_metadata(test_struct_t, a_int8_t), "int8_t");
	xtype->add_slot(slot_metadata(test_struct_t, a_int16_t), "int16_t");
	xtype->add_slot(slot_metadata(test_struct_t, a_int32_t), "int32_t");
	xtype->add_slot(slot_metadata(test_struct_t, a_int64_t), "int64_t");

	register_type(xtype);

	xnode.xadd(&ts, 1, "/basic/test", "test_basic_fmt");
	auto rs = xnode.xdump("/basic/test");
	ResultSet exp_rs = {{"uint8_t", "uint16_t", "uint32_t", "uint64_t",
						 "int8_t", "int16_t", "int32_t", "int64_t"},
						{"240", "61440", "4026531840", "17293822569102704640",
						 "-16", "-4096", "-268435456", "-1152921504606846976"}};
	assert_rs(*rs, exp_rs);
}

static void basic_test_array(XNode &xnode) {
	test_simple_t ts[] = {1,2,3,4,5};
	xnode.xadd(&ts, sizeof(ts)/sizeof(test_simple_t), "/test_array", "test_dirs_t");
	auto rs = xnode.xdump("/test_array");
	ResultSet exp_rs = {{"a"}, {"1"}, {"1"}, {"1"}, {"1"}, {"1"}};
	assert_rs(*rs, exp_rs);
}

static void basic_test_dirs()
{
	XNode xnode;
	test_simple_t ts = {1};
	shared_ptr<XType> xtype = make_shared<XType>("test_dirs_t", sizeof(test_simple_t));
	xtype->add_slot(slot_metadata(test_simple_t, a), "int");
	register_type(xtype); //TODO: register on type create?

	//TODO: use macro, and check that size of struct is equal to struct of xtype
	xnode.xadd(&ts, 1, "/dir/test1", "test_dirs_t");
	xnode.xadd(&ts, 1, "/dir/test2", "test_dirs_t");
	xnode.xadd(&ts, 1, "/dir/test3", "test_dirs_t");
	auto rs = xnode.xdump("/dir");
	ResultSet exp_rs = {{"dir"}, {"test1"}, {"test2"}, {"test3"}};
	assert_rs(*rs, exp_rs);

	auto rs_root = xnode.xdump("/");
	ResultSet exp_rs_root = {{"dir"}, {"dir"}};
	assert_rs(*rs_root, exp_rs_root);
	basic_test_array(xnode);
}

static void test_xclient() {
	XClient xclient("test-key");
	test_simple_t ts = {1};

	auto &xnode = xclient.xnode;
	xnode.xadd(&ts, 1, "/dir/test1", "test_dirs_t");
	xnode.xadd(&ts, 1, "/dir/test2", "test_dirs_t");
	xnode.xadd(&ts, 1, "/dir/test3", "test_dirs_t");

	auto rs = xclient.handle_query("/dir/test1");
	ResultSet exp_rs = {{"a"} ,{"1"}};
	assert_rs(*rs, exp_rs);

	auto rs_ping = xclient.handle_query("/..ping");
	ResultSet exp_rs_pong = {{"pong"}};
	assert_rs(*rs_ping, exp_rs_pong);
}


static string s_recv(zmq::socket_t &socket) {
	zmq::message_t message;
	socket.recv(&message);
	auto res = string(static_cast<char*>(message.data()), message.size());
	return res;
}


struct XServer_simulate {
	zmq::context_t ctx = zmq::context_t(1);
	zmq::socket_t res = zmq::socket_t(ctx, zmq::socket_type::router);

	string node_id;

	XServer_simulate();
	void recv(string &rs_json_str);
	void send(const string &query);
};

XServer_simulate::XServer_simulate() {
	//res.bind("tcp://*:" + to_string(XNODE_SERVER_PORT));
}

void XServer_simulate::recv(string &rs_json_str) {
	node_id = s_recv(res);
	string empty = s_recv(res);
	rs_json_str = s_recv(res);
}

void XServer_simulate::send(const string &query) {
	vector<string> req = {"123", query, "123"};
	json json = req;
	auto json_str = json.dump();
	res.send(node_id.c_str(), node_id.size(), ZMQ_SNDMORE);
	res.send(json_str.c_str(), json_str.size());
}

static void test_full_xclient() {
	XClient xclient = XClient("a");
	string rs_json_str;

	test_simple_t ts = {17};
	xclient.xnode.xadd(&ts, 1, "/test_full_xclient", "test_dirs_t");
	xclient.start();

	XServer_simulate xserv = XServer_simulate();


	/* hello message */
	xserv.recv(rs_json_str);
	auto req = json::parse(rs_json_str);
	string hello_rs = req[1][0][0];
	assert(hello_rs.substr(0, sizeof(HELLO_MSG_PREFIX)-1) == HELLO_MSG_PREFIX);

	/* request */
	xserv.send("/test_full_xclient");
	xserv.recv(rs_json_str);
	auto response_dir = json::parse(rs_json_str);
	ResultSet rs = response_dir[1];
	ResultSet exp_rs = {{"a"}, {"17"}};
	assert_rs(rs, exp_rs);

	/* quit */
	xserv.send("/..quit");
	xclient.xclient_thread->join();
}

static void test_xclient_timeout() {
	XClient xclient("test-key");
	string rs_json_str;

	test_simple_t ts = {17};
	xclient.xnode.xadd(&ts, 1, "/test_full_xclient", "test_dirs_t");
	xclient.rx_timeout = 900;
	xclient.init_socket();
	xclient.start();

	XServer_simulate xserv = XServer_simulate();

	/* hello message */
	xserv.recv(rs_json_str);
	sleep(1);
	xserv.recv(rs_json_str);
	/* request */
	xserv.send("/test_full_xclient");
	xserv.recv(rs_json_str);
	auto response_dir = json::parse(rs_json_str);
	ResultSet rs = response_dir[1];
	ResultSet exp_rs = {{"a"}, {"17"}};
	assert_rs(rs, exp_rs);
}

static void run_tests() {
	register_basic_types();
	basic_test_fmt();
	basic_test_dirs();

	test_xclient();
	test_full_xclient();
	test_xclient_timeout();
	types.clear();
}
#if 0
int main(int argc, char **argv) {
	if(argc == 2) {
		run_tests();
	} else {
		register_basic_types();
		test_simple_t ts = {1};

		shared_ptr<XType> xtype = make_shared<XType>("test_dirs_t");
		xtype->add_slot(slot_metadata(test_simple_t, a), "int");
		register_type(xtype); //TODO: register on type create?

		XClient xclient;
		xclient.xnode.xadd(&ts, 1, "/my_test", "test_dirs_t");
		xclient.start();
		while(true)
			sleep(1);
	}
}
#endif

/***
 * C API
 */

XClient *c_xclient;

int xray_init(const char *api_key) {
	register_basic_types();
	c_xclient = new XClient(api_key);
	c_xclient->start();
	return c_xclient != nullptr;
}

void *_xray_create_type(const char *type_name, int size, xray_fmt_type_cb fmt_type_cb) {
	 auto new_type = make_shared<XType>(type_name, size, "", fmt_type_cb);
	 register_type(new_type);
	 return (void *)new_type.get();
}

int _xray_add_slot(void *type,
				   const char *slot_name,
				   int offset, int size,
				   const char *slot_type,
				   int is_pointer,
				   int arr_size,
				   int flags) {
	auto new_type = static_cast<XType *>(type);
	if(type == nullptr)
		throw runtime_error{"type should not be null"};
	if(slot_name == nullptr)
		throw runtime_error{"slot_name should not be null"};
	if(slot_type == nullptr)
		throw runtime_error{"slot_type should not be null"};
	if(new_type->pk && (flags & XRAY_FLAG_PK))
		throw runtime_error{"type '" + new_type->name + "' already has configured pk on slot '" + new_type->pk->name + "'"};

	new_type->add_slot(slot_name, offset, size, slot_type, is_pointer, arr_size, flags);
	return 0;
}

int xray_add_vslot(void *type, const char *vslot_name, xray_vslot_fmt_cb fmt_cb) {
	auto new_type = static_cast<XType *>(type);

	if(type == nullptr)
		throw runtime_error{"type should not be null"};
	if(vslot_name == nullptr)
		throw runtime_error{"vslot_name should not be null"};

	new_type->add_vslot(vslot_name, fmt_cb, NULL);
	return 0;
}


int _xray_register(const char *type, void *obj, const char *path, int n_rows, xray_iterator iterator_cb) {

	c_xclient->xnode.xadd(obj, n_rows, path, type, iterator_cb);
	return 0;
}

void xray_dump(const char *path) {
	auto rs = c_xclient->xnode.xdump(path);
	print_rs(*rs);
}

int _xray_add_bytype(const char *type_name, void *row_dst, void *row_toadd)
{
	auto xtype = types.find(type_name);
	string str_type_name = type_name;
	if(xtype == types.end())
		throw invalid_argument("cannot add xobj, type not exists:'" + str_type_name + "'");

	for(auto &slot: xtype->second->slots) {
		// TODO: support simple formating, needed support for more complicated
		if(slot.second->flags & XRAY_FLAG_CONST)
			continue;
		int slot_offset = slot.second->offset;
		void *slot_dst_ptr = (uint8_t *)row_dst + slot_offset;
		void *slot_toadd_ptr = (uint8_t *)row_toadd + slot_offset;
		int slot_size = slot.second->type->size;
		//TODO: make signed awarenes
		add_int_value_of_slot(slot_size, 0, slot_dst_ptr, slot_toadd_ptr, slot.second->is_refernce);
	}
	return 0;
}
