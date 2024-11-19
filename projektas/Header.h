#pragma once

#include <mariadb/conncpp.hpp>
#include <memory>
#include <string>
#include <iostream>

class MySQLDatabase {
public:
    MySQLDatabase(const std::string& host, const std::string& user, const std::string& password, const std::string& db_name)
        : host_(host), user_(user), password_(password), db_name_(db_name) {}

    std::unique_ptr<sql::Connection> connect() {
        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            std::unique_ptr<sql::Connection> conn(driver->connect(host_, user_, password_));
            conn->setSchema(db_name_);
            return conn;
        }
        catch (const std::exception& e) {
            std::cerr << "Connection failed: " << e.what() << std::endl;
            throw;
        }
    }

private:
    std::string host_;
    std::string user_;
    std::string password_;
    std::string db_name_;
};