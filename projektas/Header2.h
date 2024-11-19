#pragma once

#include "Header.h"  // MySQLDatabase klasės įtraukimas
#include <string>
#include <mariadb/conncpp.hpp>
#include <memory>
#include <iostream>

class User {
public:
    User(int id, const std::string& name, const std::string& role)
        : id_(id), name_(name), role_(role) {}

    void printInfo() const {
        std::cout << "ID: " << id_ << ", Name: " << name_ << ", Role: " << role_ << std::endl;
    }

    // Getter funkcijos
    int getId() const { return id_; }
    std::string getName() const { return name_; }
    std::string getRole() const { return role_; }

    static void createUser(MySQLDatabase& db, const std::string& name, const std::string& role) {
        try {
            std::unique_ptr<sql::Connection> conn = db.connect();
            std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement("INSERT INTO users (name, role) VALUES (?, ?)"));
            stmt->setString(1, name);
            stmt->setString(2, role);
            stmt->executeUpdate();
        }
        catch (const std::exception& e) {
            std::cerr << "Error creating user: " << e.what() << std::endl;
            throw;
        }
    }

    static User getUser(MySQLDatabase& db, int id) {
        try {
            std::unique_ptr<sql::Connection> conn = db.connect();
            std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement("SELECT id, name, role FROM users WHERE id = ?"));
            stmt->setInt(1, id);
            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

            if (res->next()) {
                // Patikrinkite, ar getInt() ir getString() grąžina teisingus tipus
                int userId = res->getInt("id");  // getInt() grąžina int

                // Naudokite c_str(), kad konvertuotumėte sql::SQLString į std::string
                std::string userName = res->getString("name").c_str();  // getString() grąžina sql::SQLString
                std::string userRole = res->getString("role").c_str();  // getString() grąžina sql::SQLString

                // Sukuriame ir grąžiname User objektą
                return User(userId, userName, userRole);
            }
            else {
                throw std::runtime_error("User not found");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error fetching user: " << e.what() << std::endl;
            throw;
        }
    }

    static void updateUser(MySQLDatabase& db, int id, const std::string& name, const std::string& role) {
        try {
            std::unique_ptr<sql::Connection> conn = db.connect();
            std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement("UPDATE users SET name = ?, role = ? WHERE id = ?"));
            stmt->setString(1, name);
            stmt->setString(2, role);
            stmt->setInt(3, id);
            stmt->executeUpdate();
        }
        catch (const std::exception& e) {
            std::cerr << "Error updating user: " << e.what() << std::endl;
            throw;
        }
    }

    static void deleteUser(MySQLDatabase& db, int id) {
        try {
            std::unique_ptr<sql::Connection> conn = db.connect();
            std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement("DELETE FROM users WHERE id = ?"));
            stmt->setInt(1, id);
            stmt->executeUpdate();
        }
        catch (const std::exception& e) {
            std::cerr << "Error deleting user: " << e.what() << std::endl;
            throw;
        }
    }

private:
    int id_;
    std::string name_;
    std::string role_;
};