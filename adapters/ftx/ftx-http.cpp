#include "ftx-http.hpp"


ftxAPI::ftxAPI(executor ex, ssl::context& ctxe, net::io_context &ioce)
    : resolver_(ex),stream_(ex, ctxe),ioc(ioce),ctx(ctxe){}


http::response<http::string_body> ftxAPI::http_call(boost::url url, http::verb action) 
{

    std::string const host(url.host());
    std::string const service = url.has_port() 
        ? url.port()
        : (url.scheme_id() == boost::urls::scheme::https) 
            ? "https"
            : "http";
    url.remove_origin(); 


    if(! SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str()))
    {
        beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
        std::cerr << ec.message() << "\n";
    }

    req_.method(action);
    req_.target(url.c_str());
    req_.set(http::field::host, host);
    req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req_.set("FTX-KEY",api_key);
    req_.set("FTX-TS",std::to_string(get_ms_timestamp(current_time()).count()));
    req_.set("FTX-SIGN",sign);
    
    req_.prepare_payload();

    auto const results = resolver_.resolve(host, service);
    beast::get_lowest_layer(stream_).connect(results);

    stream_.handshake(ssl::stream_base::client);

    http::write(stream_, req_);
    http::read(stream_, buffer_, res_);
    beast::error_code ec;
    stream_.shutdown(ec);

    return res_;
}

std::string ftxAPI::authenticate(const char* key, const char* data) 
{
    unsigned char *result;
    static char res_hexstring[64];
    int result_len = 32;
    std::string signature;

    result = HMAC(EVP_sha256(), key, strlen((char *)key), const_cast<unsigned char *>(reinterpret_cast<const unsigned char*>(data)), strlen((char *)data), NULL, NULL);
    for (int i = 0; i < result_len; i++) {
        sprintf(&(res_hexstring[i * 2]), "%02x", result[i]);
    }

    for (int i = 0; i < 64; i++) {
        signature += res_hexstring[i];
    }

    return signature;
}

void ftxAPI::configure(const std::string &api,const std::string &secret)
{
    this->api_key = api;
    this->secret_key = secret;
}

json ftxAPI::list_markets()
{
    boost::url method{"markets"};
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::list_market(std::string market)
{
    boost::url method{"markets/"+market};
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::get_orderbook(std::string market, int depth)
{
    boost::url method{"markets/"+market+"/orderbook"};
    method.params().emplace_back("depth",std::to_string(depth));
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}   

json ftxAPI::get_trades(std::string market)
{
    boost::url method{"markets/"+market+"/trades"};
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::list_future(std::string future)
{
    boost::url method{"futures/"+future};
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::future_stats(std::string future)
{
    boost::url method{"futures/"+future+"/stats"};
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::account_info(std::string future)
{
    boost::url method{"account"};
    std::string data = std::to_string(get_ms_timestamp(current_time()).count()) + "GET" + "/api/account";
    sign = authenticate(secret_key.c_str(),data.c_str());
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::closed_positions()
{
    boost::url method{"positions"};
    std::string data = std::to_string(get_ms_timestamp(current_time()).count()) + "GET" + "/api/positions";
    sign = authenticate(secret_key.c_str(),data.c_str());
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::open_orders()
{
    boost::url method{"orders"};
    std::string data = std::to_string(get_ms_timestamp(current_time()).count()) + "GET" + "/api/orders";
    sign = authenticate(secret_key.c_str(),data.c_str());
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::open_orders(std::string market)
{
    boost::url method{"orders"};
    method.params().emplace_back("market",market);
    std::string data = std::to_string(get_ms_timestamp(current_time()).count()) + "GET" + "/api/orders?market="+ market;
    sign = authenticate(secret_key.c_str(),data.c_str());
    return json::parse(http_call(make_url(base_api,method),http::verb::get).body());
}

json ftxAPI::place_order(std::string market, std::string side, double size,bool ioc,bool post_only,bool reduce_only)
{
    boost::url method{"orders"};

    json payload = {{"market", market},
                {"side", side},
                {"price", NULL},
                {"type", "market"},
                {"size", size},
                {"reduceOnly", reduce_only},
                {"ioc", ioc},
                {"postOnly", post_only}};

    req_.body() = payload.dump();
    std::string data = std::to_string(get_ms_timestamp(current_time()).count()) + "POST" + "/api/orders" + payload.dump();
    sign = authenticate(secret_key.c_str(),data.c_str());
    return json::parse(http_call(make_url(base_api,method),http::verb::post).body());
}

json ftxAPI::place_order(std::string market, std::string side,double price, double size,bool ioc,bool post_only,bool reduce_only)
{
    boost::url method{"orders"};

    json payload = {{"market", market},
                {"side", side},
                {"price", price},
                {"type", "limit"},
                {"size", size},
                {"reduceOnly", reduce_only},
                {"ioc", ioc},
                {"postOnly", post_only}};

    req_.body() = payload.dump();
    std::string data = std::to_string(get_ms_timestamp(current_time()).count()) + "POST" + "/api/orders" + payload.dump();
    sign = authenticate(secret_key.c_str(),data.c_str());
    return json::parse(http_call(make_url(base_api,method),http::verb::post).body());
}

json ftxAPI::cancel_order(int orderid)
{
    boost::url method{"orders/"+std::to_string(orderid)};
    std::string data = std::to_string(get_ms_timestamp(current_time()).count()) + "DELETE" + "/api/orders/"+std::to_string(orderid);
    sign = authenticate(secret_key.c_str(),data.c_str());
    return json::parse(http_call(make_url(base_api,method),http::verb::delete_).body());
}

json ftxAPI::cancel_all_orders(std::string market)
{
    boost::url method{"orders"};
    json payload = {{"market",market}};
    req_.body() = payload.dump();
    std::string data = std::to_string(get_ms_timestamp(current_time()).count()) + "DELETE" + "/api/orders"+payload.dump();
    sign = authenticate(secret_key.c_str(),data.c_str());
    return json::parse(http_call(make_url(base_api,method),http::verb::delete_).body());
}

