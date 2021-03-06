/*
 * Copyright 2017, alex at staticlibs.net
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * File:   wiltoncall_client.cpp
 * Author: alex
 *
 * Created on January 10, 2017, 5:40 PM
 */

#include <cstdio>
#include <memory>
#include <string>

#include "staticlib/support.hpp"
#include "staticlib/json.hpp"
#include "staticlib/utils.hpp"

#include "wilton/wilton_http.h"

#include "wilton/support/exception.hpp"
#include "wilton/support/buffer.hpp"
#include "wilton/support/registrar.hpp"

namespace wilton {
namespace http {

namespace { //anonymous

// initialized from wilton_module_init
std::shared_ptr<wilton_HttpClient> shared_client() {
    static std::shared_ptr<wilton_HttpClient> client =
            std::unique_ptr<wilton_HttpClient, std::function<void(wilton_HttpClient*)>>(
            []{
                std::string cfg = sl::json::dumps({
                    {"multiThreaded", true} 
                });
                wilton_HttpClient* http;
                char* err = wilton_HttpClient_create(std::addressof(http), cfg.data(), static_cast<int> (cfg.length()));
                if (nullptr != err) {
                    support::throw_wilton_error(err, TRACEMSG(err));
                }
                return http;
            }(),
            [] (wilton_HttpClient* http) {
#ifndef STATICLIB_WINDOWS
                wilton_HttpClient_close(http);
#else // STATICLIB_WINDOWS
                // client destructor takes mutices and
                // msvcr doesn't like that in JNI mode
                (void) http;
#endif // STATICLIB_WINDOWS
            });
    return client;
}

} // namespace

support::buffer httpclient_send_request(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    auto rurl = std::ref(sl::utils::empty_string());
    auto rdata = std::ref(sl::utils::empty_string());
    std::string metadata = sl::utils::empty_string();
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("url" == name) {
            rurl = fi.as_string_nonempty_or_throw(name);
        } else if ("data" == name) {
            rdata = fi.as_string();
        } else if ("metadata" == name) {
            metadata = fi.val().dumps();
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (rurl.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'url' not specified"));
    const std::string& url = rurl.get();
    const std::string& request_data = rdata.get();
    // call wilton
    auto cl = shared_client();
    char* out = nullptr;
    int out_len = 0;
    char* err = wilton_HttpClient_execute(cl.get(), url.c_str(), static_cast<int>(url.length()),
            request_data.c_str(), static_cast<int>(request_data.length()), 
            metadata.c_str(), static_cast<int>(metadata.length()),
            std::addressof(out), std::addressof(out_len));
    if (nullptr != err) support::throw_wilton_error(err, TRACEMSG(err));
    return support::wrap_wilton_buffer(out, out_len);
}

support::buffer httpclient_send_file(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    auto rurl = std::ref(sl::utils::empty_string());
    auto rfile = std::ref(sl::utils::empty_string());
    std::string metadata = sl::utils::empty_string();
    auto rem = false;
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("url" == name) {
            rurl = fi.as_string_nonempty_or_throw(name);
        } else if ("filePath" == name) {
            rfile = fi.as_string_nonempty_or_throw(name);
        } else if ("metadata" == name) {
            metadata = fi.val().dumps();
        } else if ("remove" == name) {
            rem = fi.as_bool_or_throw(name);
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (rurl.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'url' not specified"));
    if (rfile.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'filePath' not specified"));
    const std::string& url = rurl.get();
    const std::string& file_path = rfile.get();
    // call wilton
    auto cl = shared_client();
    char* out = nullptr;
    int out_len = 0;
    std::string* pass_ctx = rem ? new std::string(file_path.data(), file_path.length()) : new std::string();
    char* err = wilton_HttpClient_send_file(cl.get(), url.c_str(), static_cast<int>(url.length()),
            file_path.c_str(), static_cast<int>(file_path.length()), 
            metadata.c_str(), static_cast<int>(metadata.length()),
            std::addressof(out), std::addressof(out_len),
            pass_ctx,
            [](void* ctx, int) {
                std::string* filePath_passed = static_cast<std::string*> (ctx);
                if (!filePath_passed->empty()) {
                    std::remove(filePath_passed->c_str());
                }
                delete filePath_passed;
            });
    if (nullptr != err) support::throw_wilton_error(err, TRACEMSG(err));
    return support::wrap_wilton_buffer(out, out_len);
}

} // namespace
}

extern "C" char* wilton_module_init() {
    try {
        wilton::http::shared_client();
        wilton::support::register_wiltoncall("httpclient_send_request", wilton::http::httpclient_send_request);
        wilton::support::register_wiltoncall("httpclient_send_file", wilton::http::httpclient_send_file);
        return nullptr;
    } catch (const std::exception& e) {
        return wilton::support::alloc_copy(TRACEMSG(e.what() + "\nException raised"));
    }
}
