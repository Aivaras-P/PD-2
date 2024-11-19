#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mariadb/conncpp.hpp>
#include <crow.h>
#include <fstream>
#include <tuple>


class User;
class Teacher;
class Student;
class Administrator;
class MySQLDatabase;

using namespace std;

// Funkcija įkrauti HTML turinį iš failo
std::string loadHTML(const std::string& filename) {
    std::ifstream file(filename);
    std::string content;

    if (file) {
        content.assign((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }
    else {
        std::cerr << "Nepavyko įkrauti HTML failo: " << filename << std::endl;
    }

    return content;
}



class User {
public:
    // Konstuktorius
    User(int id, const string& name, const string& surname, const string& role)
        : id_(id), name_(name), surname_(surname), role_(role) {
        // Sukuriame prisijungimo vardą.
        username_ = string(1, tolower(name_[0])) + name_.substr(1);
        // Sukuriame slaptažodį: pavardė
        password_ = surname_;
    }

    // Getter'iai
    int getId() const { return id_; }
    string getName() const { return name_; }
    string getSurname() const { return surname_; }
    string getRole() const { return role_; }
    string getUsername() const { return username_; }
    string getPassword() const { return password_; }

private:
    int id_;
    string name_;
    string surname_;
    string role_;
    string username_;
    string password_;
};

class MySQLDatabase {
public:
    MySQLDatabase(const std::string& host, const std::string& user, const std::string& password, const std::string& db)
        : host_(host), user_(user), password_(password), db_(db) {}

    // Funkcija, kuri tikrina vartotoją pagal username ir password, ir grąžina vartotojo vaidmenį bei ID
    std::pair<std::string, int> validateUser(const std::string& username, const std::string& password) {
        sql::Connection* con = connect();
        if (con) {
            try {
                // Patikrinti studentų lentelę
                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
                    "SELECT role, student_id FROM students WHERE username = ? AND password = ?"));
                pstmt->setString(1, username);
                pstmt->setString(2, password);
                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
                if (res->next()) {
                    return { "Studentas", res->getInt("student_id") };  // Grąžinsime studento ID
                }

                // Patikrinti dėstytojų lentelę
                pstmt = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(
                    "SELECT role, teacher_id FROM teachers WHERE username = ? AND password = ?"));
                pstmt->setString(1, username);
                pstmt->setString(2, password);
                res = std::unique_ptr<sql::ResultSet>(pstmt->executeQuery());
                if (res->next()) {
                    return { "Destytojas", res->getInt("teacher_id") };  // Grąžinsime dėstytojo ID
                }

                // Patikrinti administratorių lentelę
                pstmt = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(
                    "SELECT role FROM administrator WHERE username = ? AND password = ?"));
                pstmt->setString(1, username);
                pstmt->setString(2, password);
                res = std::unique_ptr<sql::ResultSet>(pstmt->executeQuery());
                if (res->next()) {
                    return { "Administratorius", -1 };  // Administratoriams negrąžiname ID, nes jie neturi `student_id` ar `teacher_id`
                }

            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida tikrinant vartotoją: " << e.what() << std::endl;
            }
            delete con;
        }
        return { "", -1 };  // Jei vartotojas nerastas, grąžinsime tuščią rolę ir klaidingą ID
    }

    // Funkcija, kad suskaidytume POST užklausą į parametrus (prisijungimui)
    void parseLoginData(const std::string& body, std::string& username, std::string& password) {
        std::istringstream stream(body);
        std::string param;
        while (std::getline(stream, param, '&')) {
            size_t pos = param.find("=");
            if (pos != std::string::npos) {
                std::string key = param.substr(0, pos);
                std::string value = param.substr(pos + 1);

                if (key == "username") {
                    username = value;
                }
                else if (key == "password") {
                    password = value;
                }
            }
        }
    }



    sql::Connection* connect() {
        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();
            sql::Connection* con = driver->connect("tcp://" + host_ + ":3306", user_, password_);
            con->setSchema(db_);
            return con;
        }
        catch (sql::SQLException& e) {
            std::cerr << "MariaDB klaida: " << e.what() << std::endl;
            return nullptr;
        }
    }

private:
    std::string host_;
    std::string user_;
    std::string password_;
    std::string db_;
};

//Studento klasė (vaikas iš user klasės)
class Student : public User {
public:
    Student(int id, const string& name, const string& surname)
        : User(id, name, surname, "Studentas") {}

    // Metodas grąžinantis studento duomenis
    static bool getStudentData(int student_id, sql::Connection* con, std::string& name, std::string& surname, std::vector<std::pair<std::string, std::string>>& subjects) {
        // Pirma užklausa: gauti studento vardą ir pavardę
        std::unique_ptr<sql::PreparedStatement> studentPstmt(con->prepareStatement(
            "SELECT name, surname FROM students WHERE student_id = ?"
        ));
        studentPstmt->setInt(1, student_id);

        std::unique_ptr<sql::ResultSet> studentRes(studentPstmt->executeQuery());

        if (studentRes->next()) {
            name = studentRes->getString("name").c_str();
            surname = studentRes->getString("surname").c_str();
        }
        else {
            return false;  // Studentas nerastas
        }

        // Antra užklausa: gauti studento studijojamus dalykus ir pažymius
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "SELECT sub.subject_name, "
            "CASE WHEN g.grade IS NULL THEN 'Nera' ELSE CAST(g.grade AS CHAR) END AS grade "
            "FROM students_subjects ss "
            "JOIN subjects sub ON ss.subject_id = sub.subject_id "
            "LEFT JOIN grades g ON ss.student_id = g.student_id AND sub.subject_id = g.subject_id "
            "WHERE ss.student_id = ?"
        ));
        pstmt->setInt(1, student_id);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        // Užpildome dalykų ir pažymių sąrašą
        while (res->next()) {
            std::string subject_name = res->getString("subject_name").c_str();
            std::string grade = res->getString("grade").c_str();
            subjects.push_back({ subject_name, grade });
        }

        return true;
    }

};
// Dėstytojo klasė (vaikas iš user klasės)
class Teacher : public User {
public:
    Teacher(int id, const string& name, const string& surname)
        : User(id, name, surname, "Destytojas") {}
    // Funkcija gauti dėstytojo informaciją
    static bool getTeacherInfo(int teacher_id, sql::Connection* con, std::string& name, std::string& surname) {
        // Gauti dėstytojo vardą ir pavardę
        std::unique_ptr<sql::PreparedStatement> teacherPstmt(con->prepareStatement(
            "SELECT name, surname FROM teachers WHERE teacher_id = ?"
        ));
        teacherPstmt->setInt(1, teacher_id);
        std::unique_ptr<sql::ResultSet> teacherRes(teacherPstmt->executeQuery());

        if (teacherRes->next()) {
            name = teacherRes->getString("name").c_str();
            surname = teacherRes->getString("surname").c_str();
            return true;
        }
        return false;
    }

    static bool getSubjects(int teacher_id, sql::Connection* con, std::vector<std::pair<int, std::string>>& subjects) {
        // Gauti dėstytojui priskirtus dalykus
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "SELECT s.subject_id, s.subject_name "
            "FROM teacher_subjects ts "
            "JOIN subjects s ON ts.subject_id = s.subject_id "
            "WHERE ts.teacher_id = ?"
        ));
        pstmt->setInt(1, teacher_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            int subject_id = res->getInt("subject_id");
            std::string subject_name = res->getString("subject_name").c_str();
            subjects.push_back({ subject_id, subject_name });
        }

        return !subjects.empty();
    }
    // Funkcija gauti dėstomo dalyko informaciją.
    static bool getSubjectInfo(int subject_id, sql::Connection* con, std::string& subject_name) {
        std::unique_ptr<sql::PreparedStatement> pstmt_subject(con->prepareStatement(
            "SELECT subject_name FROM subjects WHERE subject_id = ?"));
        pstmt_subject->setInt(1, subject_id);
        std::unique_ptr<sql::ResultSet> res_subject(pstmt_subject->executeQuery());

        if (res_subject->next()) {
            subject_name = res_subject->getString("subject_name").c_str();
            return true;
        }
        return false;
    }
    // Funkcija gauti studentus iš dėstomo dalyko.
    static bool getStudentsForSubject(int subject_id, sql::Connection* con, std::string& students_html) {
        std::unique_ptr<sql::PreparedStatement> pstmt_students(con->prepareStatement(
            "SELECT DISTINCT s.student_id, s.name, s.surname, IFNULL(g.grade, 0) AS grade "
            "FROM students s "
            "JOIN students_subjects ss ON s.student_id = ss.student_id "
            "JOIN group_subjects gs ON gs.subject_id = ss.subject_id "
            "LEFT JOIN grades g ON s.student_id = g.student_id AND g.subject_id = ss.subject_id "
            "WHERE ss.subject_id = ? AND s.group_id IS NOT NULL"));
        pstmt_students->setInt(1, subject_id);
        std::unique_ptr<sql::ResultSet> res_students(pstmt_students->executeQuery());

        while (res_students->next()) {
            int student_id = res_students->getInt("student_id");
            std::string name = res_students->getString("name").c_str();
            std::string surname = res_students->getString("surname").c_str();
            int grade = res_students->getInt("grade");

            students_html += "<tr>";
            students_html += "<td>" + std::to_string(student_id) + "</td>";
            students_html += "<td>" + name + "</td>";
            students_html += "<td>" + surname + "</td>";
            students_html += "<td>" + (grade == 0 ? "Nera" : std::to_string(grade)) + "</td>";
            students_html += "</tr>";
        }

        return !students_html.empty();
    }
    // Funkcija tikrinanti studentus iš DB.
    static bool checkStudentExistence(int student_id, sql::Connection* con) {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "SELECT 1 FROM students WHERE student_id = ?"));
        pstmt->setInt(1, student_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        return res->next();
    }
    // Funkcija tikrinanti studentus ir dėstomus dalykus iš DB.
    static bool checkStudentSubjectAssignment(int student_id, int subject_id, sql::Connection* con) {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "SELECT 1 FROM students_subjects WHERE student_id = ? AND subject_id = ?"));
        pstmt->setInt(1, student_id);
        pstmt->setInt(2, subject_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        return res->next();
    }
    // Funkcija tikrinanti studentų pažymius iš DB.
    static bool checkStudentGradeExistence(int student_id, int subject_id, sql::Connection* con) {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "SELECT grade FROM grades WHERE student_id = ? AND subject_id = ?"));
        pstmt->setInt(1, student_id);
        pstmt->setInt(2, subject_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        return res->next();
    }
    // Funkcija leidžianti pridėti studentui pažymį.
    static bool addGrade(int student_id, int subject_id, int grade, sql::Connection* con) {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "INSERT INTO grades (student_id, subject_id, grade) VALUES (?, ?, ?)"));
        pstmt->setInt(1, student_id);
        pstmt->setInt(2, subject_id);
        pstmt->setInt(3, grade);
        pstmt->executeUpdate();
        return true;
    }
    // Funkcija leidžianti panaikinti studento pažymį.
    static bool deleteGrade(int student_id, int subject_id, sql::Connection* con) {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "DELETE FROM grades WHERE student_id = ? AND subject_id = ?"));
        pstmt->setInt(1, student_id);
        pstmt->setInt(2, subject_id);
        pstmt->executeUpdate();
        return true;
    }
    // Funkcija skirta gauti pažymius.
    static int getCurrentGrade(int student_id, int subject_id, sql::Connection* con) {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "SELECT grade FROM grades WHERE student_id = ? AND subject_id = ?"));
        pstmt->setInt(1, student_id);
        pstmt->setInt(2, subject_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (res->next()) {
            return res->getInt("grade");
        }
        return -1; // -1 reiškia, kad pažymio nėra
    }
    // Funkcija leidžianti koreguoti studento pažymį.
    static bool updateGrade(int student_id, int new_grade, int subject_id, sql::Connection* con) {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
            "UPDATE grades SET grade = ? WHERE student_id = ? AND subject_id = ?"));
        pstmt->setInt(1, new_grade);
        pstmt->setInt(2, student_id);
        pstmt->setInt(3, subject_id);
        pstmt->executeUpdate();
        return true;
    }

};

// Admino klasė (vaikas iš user klasės)
class Administrator : public User {
public:
    Administrator(int id, const string& name, const string& surname)
        : User(id, name, surname, "Administratorius") {}

    // Pridėti studentą į duomenų bazę
    std::string addStudentToDatabase(MySQLDatabase& db, const std::string& name, const std::string& surname) {
        sql::Connection* con = db.connect();
        if (con) {
            try {
                // Patikriname, ar studentas su tokiu vardu ir pavarde jau egzistuoja
                unique_ptr<sql::PreparedStatement> pstmt_student(
                    con->prepareStatement("SELECT COUNT(*) FROM students WHERE name = ? AND surname = ?"));
                pstmt_student->setString(1, name);
                pstmt_student->setString(2, surname);
                unique_ptr<sql::ResultSet> res_student(pstmt_student->executeQuery());
                if (res_student->next() && res_student->getInt(1) > 0) {
                    delete con;
                    return "Studentas su tokiu vardu ir pavarde jau egzistuoja!";
                }

                // Patikriname, ar dėstytojas su tokiu vardu ir pavarde jau egzistuoja
                unique_ptr<sql::PreparedStatement> pstmt_teacher(
                    con->prepareStatement("SELECT COUNT(*) FROM teachers WHERE name = ? AND surname = ?"));
                pstmt_teacher->setString(1, name);
                pstmt_teacher->setString(2, surname);
                unique_ptr<sql::ResultSet> res_teacher(pstmt_teacher->executeQuery());
                if (res_teacher->next() && res_teacher->getInt(1) > 0) {
                    delete con;
                    return "Su tokiu vardu ir pavarde jau egzistuoja destytojas!";
                }

                // Pridėti studentą į duomenų bazę
                unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
                    "INSERT INTO students (name, surname, role, username, password) VALUES (?, ?, ?, ?, ?)"));
                pstmt->setString(1, name);
                pstmt->setString(2, surname);
                pstmt->setString(3, "Studentas");  // rolė - Studentas
                pstmt->setString(4, name.substr(0, 1) + name.substr(1));  // username (pirmoji vardo raidė)
                pstmt->setString(5, surname);  // Slaptažodis = pavardė

                pstmt->executeUpdate();
                delete con;
                return "Studentas pridetas sekmingai!";
            }
            catch (sql::SQLException& e) {
                delete con;
                return "Klaida uzklausoje: " + std::string(e.what());
            }
        }
        else {
            return "Nepavyko prisijungti prie duomenu bazes.";
        }
    }

    // Pašalinti studentą pagal ID iš visų susijusių lentelių (iš DB)
    std::string deleteStudentFromDatabase(MySQLDatabase& db, int student_id) {
        sql::Connection* con = db.connect();
        if (con) {
            try {
                // Patikriname, ar studentas egzistuoja
                unique_ptr<sql::PreparedStatement> pstmt_check(
                    con->prepareStatement("SELECT COUNT(*) FROM students WHERE student_id = ?"));
                pstmt_check->setInt(1, student_id);
                unique_ptr<sql::ResultSet> res_check(pstmt_check->executeQuery());
                if (res_check->next() && res_check->getInt(1) == 0) {
                    delete con;
                    return "Studentas su tokiu ID nerastas!";
                }

                // Pašaliname studentą iš grupės
                unique_ptr<sql::PreparedStatement> pstmt_delete_group(
                    con->prepareStatement("DELETE FROM group_students WHERE student_id = ?"));
                pstmt_delete_group->setInt(1, student_id);
                pstmt_delete_group->executeUpdate();

                // Pašaliname studentą iš dalyko
                unique_ptr<sql::PreparedStatement> pstmt_delete_subject(
                    con->prepareStatement("DELETE FROM students_subjects WHERE student_id = ?"));
                pstmt_delete_subject->setInt(1, student_id);
                pstmt_delete_subject->executeUpdate();

                // Pašaliname studentą iš pažymių
                unique_ptr<sql::PreparedStatement> pstmt_delete_grades(
                    con->prepareStatement("DELETE FROM grades WHERE student_id = ?"));
                pstmt_delete_grades->setInt(1, student_id);
                pstmt_delete_grades->executeUpdate();

                // Pašaliname studentą iš 'students' lentelės
                unique_ptr<sql::PreparedStatement> pstmt_delete_student(
                    con->prepareStatement("DELETE FROM students WHERE student_id = ?"));
                pstmt_delete_student->setInt(1, student_id);
                pstmt_delete_student->executeUpdate();

                delete con;
                return "Studentas pasalintas sekmingai!";
            }
            catch (sql::SQLException& e) {
                delete con;
                return "Klaida trinant studenta: " + std::string(e.what());
            }
        }
        else {
            return "Nepavyko prisijungti prie duomenu bazes.";
        }
    }

    // Gauti visus studentus iš duomenų bazės
    std::vector<std::tuple<int, std::string, std::string>> getAllStudents(sql::Connection* conn) {
        std::vector<std::tuple<int, std::string, std::string>> students;

        if (conn) {
            try {
                std::unique_ptr<sql::PreparedStatement> pstmt(
                    conn->prepareStatement("SELECT student_id, name, surname FROM students")
                );
                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                while (res->next()) {
                    int student_id = res->getInt("student_id");
                    std::string name = res->getString("name").c_str();
                    std::string surname = res->getString("surname").c_str();

                    students.push_back(std::make_tuple(student_id, name, surname));
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida užklausoje: " << e.what() << std::endl;
            }
        }
        else {
            std::cerr << "Nepavyko prisijungti prie duomenų bazės!" << std::endl;
        }

        return students;
    }


    // Pridėti dėstytoją į duomenų bazę
    std::string addTeacherToDatabase(MySQLDatabase& db, const std::string& name, const std::string& surname) {
        sql::Connection* con = db.connect();
        if (!con) {
            return "Klaida: nepavyko prisijungti prie duomenų bazės.";
        }

        try {
            // Patikrinti, ar dėstytojas jau egzistuoja
            unique_ptr<sql::PreparedStatement> checkTeacherStmt(
                con->prepareStatement("SELECT COUNT(*) FROM teachers WHERE name = ? AND surname = ?")
            );
            checkTeacherStmt->setString(1, name);
            checkTeacherStmt->setString(2, surname);
            unique_ptr<sql::ResultSet> teacherRes(checkTeacherStmt->executeQuery());
            if (teacherRes->next() && teacherRes->getInt(1) > 0) {
                delete con;
                return "Dėstytojas jau egzistuoja: " + name + " " + surname;
            }

            // Patikrinti, ar studentas jau egzistuoja su tokiu pačiu vardu ir pavarde
            unique_ptr<sql::PreparedStatement> checkStudentStmt(
                con->prepareStatement("SELECT COUNT(*) FROM students WHERE name = ? AND surname = ?")
            );
            checkStudentStmt->setString(1, name);
            checkStudentStmt->setString(2, surname);
            unique_ptr<sql::ResultSet> studentRes(checkStudentStmt->executeQuery());
            if (studentRes->next() && studentRes->getInt(1) > 0) {
                delete con;
                return "Studentas su tokiu vardu ir pavarde jau egzistuoja: " + name + " " + surname;
            }

            // Pridėti naują dėstytoją
            unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
                "INSERT INTO teachers (name, surname, role, username, password) VALUES (?, ?, ?, ?, ?)"));
            pstmt->setString(1, name);
            pstmt->setString(2, surname);
            pstmt->setString(3, "Destytojas");  // Rolė - Studentas
            pstmt->setString(4, name.substr(0, 1) + name.substr(1));  // username (pirmoji vardo raidė)
            pstmt->setString(5, surname);  // Slaptažodis = pavardė

            pstmt->executeUpdate();
            delete con;
            return "Destytojas pridėtas sėkmingai!";
        }
        catch (sql::SQLException& e) {
            delete con;
            return "Klaida pridedant dėstytoją: " + std::string(e.what());
        }
    }

    // Metodas, kuris patikrina, ar dėstytojas egzistuoja ir pašalina jį, (jei taip)
    std::string removeTeacherFromDatabase(MySQLDatabase& db, int teacher_id) {
        sql::Connection* con = db.connect();
        if (con) {
            try {
                // Patikriname, ar dėstytojas egzistuoja
                unique_ptr<sql::PreparedStatement> pstmt_check(
                    con->prepareStatement("SELECT COUNT(*) FROM teachers WHERE teacher_id = ?"));
                pstmt_check->setInt(1, teacher_id);
                unique_ptr<sql::ResultSet> res(pstmt_check->executeQuery());
                if (res->next() && res->getInt(1) == 0) {
                    delete con;
                    return "Destytojas su tokiu ID nerastas!";
                }

                // Pirmiausia pašaliname visus priskyrimus į teacher_subjects lentelę
                unique_ptr<sql::PreparedStatement> pstmt_delete_teacher_subjects(
                    con->prepareStatement("DELETE FROM teacher_subjects WHERE teacher_id = ?")
                );
                pstmt_delete_teacher_subjects->setInt(1, teacher_id);
                pstmt_delete_teacher_subjects->executeUpdate();

                // Pašaliname dėstytoją iš teachers lentelės
                unique_ptr<sql::PreparedStatement> pstmt_delete_teacher(
                    con->prepareStatement("DELETE FROM teachers WHERE teacher_id = ?")
                );
                pstmt_delete_teacher->setInt(1, teacher_id);
                pstmt_delete_teacher->executeUpdate();

                delete con;
                return "Destytojas pasalintas sekmingai!";
            }
            catch (sql::SQLException& e) {
                delete con;
                return "Klaida trinant destytoja: " + std::string(e.what());
            }
        }
        else {
            return "Nepavyko prisijungti prie duomenų bazės.";
        }
    }
        
    
        
    // Randame dėstytojų info iš DB.
    std::vector<std::tuple<int, std::string, std::string>> getAllTeachers(sql::Connection* con) {
        std::vector<std::tuple<int, std::string, std::string>> teachers;

        try {
            unique_ptr<sql::PreparedStatement> pstmt(
                con->prepareStatement("SELECT teacher_id, name, surname FROM teachers")
            );
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            while (res->next()) {
                int teacher_id = res->getInt("teacher_id");
                std::string name = res->getString("name").c_str();
                std::string surname = res->getString("surname").c_str();

                teachers.emplace_back(teacher_id, name, surname);
            }
        }
        catch (sql::SQLException& e) {
            std::cerr << "Klaida gaunant dėstytojus: " << e.what() << std::endl;
        }

        return teachers;
    }

    // Pridėti dėstytoją į duomenų bazę
    std::string addSubjectToDatabase(MySQLDatabase& db, const std::string& subject_name) {
        sql::Connection* con = db.connect();
        if (con) {
            try {
                // Patikrinimas, ar toks dalykas jau egzistuoja
                unique_ptr<sql::PreparedStatement> pstmt_check(
                    con->prepareStatement("SELECT COUNT(*) FROM subjects WHERE subject_name = ?")
                );
                pstmt_check->setString(1, subject_name);
                unique_ptr<sql::ResultSet> res_check(pstmt_check->executeQuery());

                if (res_check->next() && res_check->getInt(1) > 0) {
                    // Jei dalykas jau egzistuoja
                    return "Toks dalykas jau egzistuoja!";  // Pranešimas apie klaidą
                }

                // Jei dalykas neegzistuoja, pridedame į duomenų bazę
                unique_ptr<sql::PreparedStatement> pstmt_insert(
                    con->prepareStatement("INSERT INTO subjects (subject_name) VALUES (?)")
                );
                pstmt_insert->setString(1, subject_name);
                pstmt_insert->executeUpdate();

                return "Dalykas pridėtas sėkmingai!";  // Sėkmės pranešimas
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida įrašant dalyką: " << e.what() << std::endl;
                return "Įvyko klaida pridedant dalyką į duomenų bazę.";  // Klaidos pranešimas
            }
        }
        else {
            return "Nepavyko prisijungti prie duomenų bazės.";  // Klaida dėl ryšio su DB
        }
    }
    // Ištrinti dėstomą dalyką iš DB.
    std::string deleteSubjectFromDatabase(MySQLDatabase& db, int subject_id) {
        sql::Connection* con = db.connect();
        if (con) {
            try {
                // 1. Patikrinimas, ar dalykas egzistuoja
                unique_ptr<sql::PreparedStatement> pstmt_check(
                    con->prepareStatement("SELECT COUNT(*) FROM subjects WHERE subject_id = ?")
                );
                pstmt_check->setInt(1, subject_id);
                unique_ptr<sql::ResultSet> res_check(pstmt_check->executeQuery());

                if (res_check->next() && res_check->getInt(1) == 0) {
                    return "Dalykas su ID " + std::to_string(subject_id) + " nerastas.";
                }

                // 2. Pašaliname įrašus iš `grades` lentelės, susijusius su šiuo `subject_id`
                unique_ptr<sql::PreparedStatement> pstmt_delete_grades(
                    con->prepareStatement("DELETE FROM grades WHERE subject_id = ?")
                );
                pstmt_delete_grades->setInt(1, subject_id);
                pstmt_delete_grades->executeUpdate();

                // 3. Pašaliname įrašus iš `group_subjects` lentelės, susijusius su šiuo `subject_id`
                unique_ptr<sql::PreparedStatement> pstmt_delete_group_subjects(
                    con->prepareStatement("DELETE FROM group_subjects WHERE subject_id = ?")
                );
                pstmt_delete_group_subjects->setInt(1, subject_id);
                pstmt_delete_group_subjects->executeUpdate();

                // 4. Pašaliname įrašus iš `students_subjects` lentelės, susijusius su šiuo `subject_id`
                unique_ptr<sql::PreparedStatement> pstmt_delete_students_subjects(
                    con->prepareStatement("DELETE FROM students_subjects WHERE subject_id = ?")
                );
                pstmt_delete_students_subjects->setInt(1, subject_id);
                pstmt_delete_students_subjects->executeUpdate();

                // 5. Pašaliname įrašus iš `teacher_subjects` lentelės, susijusius su šiuo `subject_id`
                unique_ptr<sql::PreparedStatement> pstmt_delete_teacher_subjects(
                    con->prepareStatement("DELETE FROM teacher_subjects WHERE subject_id = ?")
                );
                pstmt_delete_teacher_subjects->setInt(1, subject_id);
                pstmt_delete_teacher_subjects->executeUpdate();

                // 6. Pašaliname dalyką iš `subjects` lentelės
                unique_ptr<sql::PreparedStatement> pstmt_delete_subject(
                    con->prepareStatement("DELETE FROM subjects WHERE subject_id = ?")
                );
                pstmt_delete_subject->setInt(1, subject_id);
                pstmt_delete_subject->executeUpdate();

                // 7. Jei viskas sėkmingai atlikta, grąžiname pranešimą apie sėkmingą pašalinimą
                return "Dalykas su ID " + std::to_string(subject_id) + " pašalintas sėkmingai.";
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida trinant dalyką: " << e.what() << std::endl;
                return "Įvyko klaida trinant dalyką: " + std::string(e.what());
            }
        }
        else {
            return "Nepavyko prisijungti prie duomenų bazės.";
        }
    }
    // Gauname visus dėstomus dalykus iš DB.
    std::vector<std::tuple<int, std::string>> getSubjectsFromDatabase(sql::Connection* con) {
        std::vector<std::tuple<int, std::string>> subjects;

        if (con) {
            try {
                unique_ptr<sql::PreparedStatement> pstmt(
                    con->prepareStatement("SELECT subject_id, subject_name FROM subjects")
                );
                unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                // Įkrauname duomenis į vektorių
                while (res->next()) {
                    int subject_id = res->getInt("subject_id");
                    std::string subject_name = res->getString("subject_name").c_str();

                    // Pridedame tuple į vektorių
                    subjects.push_back(std::make_tuple(subject_id, subject_name));
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida užklausoje: " << e.what() << std::endl;
            }
        }
        else {
            std::cerr << "Nepavyko prisijungti prie duomenų bazės!" << std::endl;
        }

        return subjects;  // Grąžiname vektorių su dalykais
    }

    // Pridėjimo grupės funkcija (gavimas duomenų iš db). 
    std::string addGroupToDatabase(MySQLDatabase& db, const std::string& group_name) {
        sql::Connection* con = db.connect();
        bool group_exists = false;
        std::string response_body;

        try {
            // Patikriname, ar grupė jau egzistuoja
            unique_ptr<sql::PreparedStatement> pstmt(
                con->prepareStatement("SELECT COUNT(*) FROM stud_groups WHERE group_name = ?")
            );
            pstmt->setString(1, group_name);
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
            if (res->next() && res->getInt(1) > 0) {
                group_exists = true;
            }
        }
        catch (sql::SQLException& e) {
            std::cerr << "Klaida užklausoje: " << e.what() << std::endl;
        }

        if (group_exists) {
            // Jei grupė jau egzistuoja
            response_body = "<html><head><meta http-equiv='refresh' content='2; url=/groups'></head><body>"
                "Tokia grupe jau egzistuoja! "
                "<br></body></html>";
        }
        else {
            // Jei grupė neegzistuoja, pridedame ją į duomenų bazę
            try {
                unique_ptr<sql::PreparedStatement> pstmt_insert(
                    con->prepareStatement("INSERT INTO stud_groups (group_name) VALUES (?)")
                );
                pstmt_insert->setString(1, group_name);
                pstmt_insert->executeUpdate();

                response_body = "<html><head><meta http-equiv='refresh' content='2; url=/groups'></head><body>"
                    "Grupe prideta sekmingai! "
                    "<br></body></html>";
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida uzklausoje irasant grupe: " << e.what() << std::endl;
                response_body = "<html><body>Klaida pridedant grupe.</body></html>";
            }
        }

        return response_body;
    }
    // Metodas panaikinti grupes iš DB.
    std::string removeGroupFromDatabase(MySQLDatabase& db, int group_id) {
        sql::Connection* con = db.connect();
        std::string result_message;

        if (con) {
            try {
                // 1. Patikriname, ar grupė su nurodytu ID egzistuoja
                unique_ptr<sql::PreparedStatement> pstmt_check(
                    con->prepareStatement("SELECT COUNT(*) FROM stud_groups WHERE group_id = ?")
                );
                pstmt_check->setInt(1, group_id);
                unique_ptr<sql::ResultSet> res(pstmt_check->executeQuery());

                if (res->next() && res->getInt(1) == 0) {
                    result_message = "Grupe su tokiu ID nerasta!";
                    return result_message;
                }

                // 2. Pašaliname group_id iš students lentelės
                unique_ptr<sql::PreparedStatement> pstmt_update_students(
                    con->prepareStatement("UPDATE students SET group_id = NULL WHERE group_id = ?")
                );
                pstmt_update_students->setInt(1, group_id);
                pstmt_update_students->executeUpdate();

                // 3. Pašaliname susijusius įrašus iš group_subjects lentelės
                unique_ptr<sql::PreparedStatement> pstmt_delete_group_subjects(
                    con->prepareStatement("DELETE FROM group_subjects WHERE group_id = ?")
                );
                pstmt_delete_group_subjects->setInt(1, group_id);
                pstmt_delete_group_subjects->executeUpdate();

                // 4. Pašaliname susijusius įrašus iš group_students lentelės
                unique_ptr<sql::PreparedStatement> pstmt_delete_group_students(
                    con->prepareStatement("DELETE FROM group_students WHERE group_id = ?")
                );
                pstmt_delete_group_students->setInt(1, group_id);
                pstmt_delete_group_students->executeUpdate();

                // 5. Pašaliname grupę iš stud_groups lentelės
                unique_ptr<sql::PreparedStatement> pstmt_delete_group(
                    con->prepareStatement("DELETE FROM stud_groups WHERE group_id = ?")
                );
                pstmt_delete_group->setInt(1, group_id);
                pstmt_delete_group->executeUpdate();

                result_message = "Grupe su ID " + std::to_string(group_id) + " pasalinta sekmingai.";
            }
            catch (sql::SQLException& e) {
                result_message = "Klaida trinant grupe: " + std::string(e.what());
            }
        }
        delete con; // Uždaryti ryšį su duomenų baze
        return result_message;
    }

    // Funkcija, kuri gauna grupių informaciją iš duomenų bazės ir grąžina vektorių su grupėmis
    std::vector<std::tuple<int, std::string>> getAllGroupsFromDatabase(sql::Connection* con) {
        std::vector<std::tuple<int, std::string>> groups;

        if (con) {
            try {
                // Atlieka užklausą, kad gautų grupių informaciją
                unique_ptr<sql::PreparedStatement> pstmt(
                    con->prepareStatement("SELECT group_id, group_name FROM stud_groups")
                );
                unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                // Užpildo vektorių su grupių ID ir pavadinimais
                while (res->next()) {
                    int group_id = res->getInt("group_id");
                    std::string group_name = res->getString("group_name").c_str();

                    groups.push_back(std::make_tuple(group_id, group_name));  // Prideda į vektorių kaip tuple
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida užklausoje: " << e.what() << std::endl;
            }
        }
        return groups;  // Grąžina vektorių su grupėmis
    }

    // Pridėti dėstytoją į duomenų bazę
    std::string addTeacherAndSubjectToDatabase(MySQLDatabase& db, int teacher_id, int subject_id) {
        // Prisijungimas prie MariaDB duomenų bazės
        sql::Connection* con = db.connect();

        if (!con) {
            return "Klaida: Nepavyko prisijungti prie duomenu bazes.";
        }

        try {
            // 1. Tikriname, ar teacher_id egzistuoja
            std::unique_ptr<sql::PreparedStatement> check_teacher(con->prepareStatement(
                "SELECT COUNT(*) FROM teachers WHERE teacher_id = ?"));
            check_teacher->setInt(1, teacher_id);
            std::unique_ptr<sql::ResultSet> teacher_res(check_teacher->executeQuery());
            bool teacher_exists = teacher_res->next() && teacher_res->getInt(1) > 0;

            // 2. Tikriname, ar subject_id egzistuoja
            std::unique_ptr<sql::PreparedStatement> check_subject(con->prepareStatement(
                "SELECT COUNT(*) FROM subjects WHERE subject_id = ?"));
            check_subject->setInt(1, subject_id);
            std::unique_ptr<sql::ResultSet> subject_res(check_subject->executeQuery());
            bool subject_exists = subject_res->next() && subject_res->getInt(1) > 0;

            // 3. Jei bet kuris neegzistuoja, grąžiname klaidą
            if (!teacher_exists) {
                delete con;
                return "Klaida: Destytojas su ID " + std::to_string(teacher_id) + " neegzistuoja.";
            }

            if (!subject_exists) {
                delete con;
                return "Klaida: Dalykas su ID " + std::to_string(subject_id) + " neegzistuoja.";
            }

            // 4. Patikriname, ar jau egzistuoja ryšys tarp dėstytojo ir dalyko
            std::unique_ptr<sql::PreparedStatement> check_relation(con->prepareStatement(
                "SELECT COUNT(*) FROM teacher_subjects WHERE teacher_id = ? AND subject_id = ?"));
            check_relation->setInt(1, teacher_id);
            check_relation->setInt(2, subject_id);
            std::unique_ptr<sql::ResultSet> relation_res(check_relation->executeQuery());
            bool relation_exists = relation_res->next() && relation_res->getInt(1) > 0;

            if (relation_exists) {
                delete con;
                return "Klaida: Sis dalykas jau priskirtas destytojui su ID " + std::to_string(teacher_id) + ".";
            }

            // 5. Jei visi patikrinimai praeina, atliekame įrašą į teacher_subjects
            std::unique_ptr<sql::PreparedStatement> insert_relation(con->prepareStatement(
                "INSERT INTO teacher_subjects (teacher_id, subject_id) VALUES (?, ?)"));
            insert_relation->setInt(1, teacher_id);
            insert_relation->setInt(2, subject_id);
            insert_relation->executeUpdate();

            delete con; // Atlaisviname jungtį
            return "success"; // Visos operacijos buvo atliktos sėkmingai
        }
        catch (sql::SQLException& e) {
            delete con; // Atlaisviname jungtį klaidos atveju
            return "SQL klaida: " + std::string(e.what());
        }
    }

    // Pašalinti dėstytoją iš dalyko
    std::string removeTeacherAndSubjectFromDatabase(MySQLDatabase& db, int teacher_id, int subject_id) {
        // Prisijungimas prie MariaDB duomenų bazės
        sql::Connection* con = db.connect();

        if (!con) {
            return "Klaida: Nepavyko prisijungti prie duomenu bazes.";
        }

        try {
            // 1. Patikriname, ar egzistuoja dėstytojas ir dalykas
            std::unique_ptr<sql::PreparedStatement> check_teacher(con->prepareStatement(
                "SELECT COUNT(*) FROM teachers WHERE teacher_id = ?"));
            check_teacher->setInt(1, teacher_id);
            std::unique_ptr<sql::ResultSet> teacher_res(check_teacher->executeQuery());
            bool teacher_exists = teacher_res->next() && teacher_res->getInt(1) > 0;

            std::unique_ptr<sql::PreparedStatement> check_subject(con->prepareStatement(
                "SELECT COUNT(*) FROM subjects WHERE subject_id = ?"));
            check_subject->setInt(1, subject_id);
            std::unique_ptr<sql::ResultSet> subject_res(check_subject->executeQuery());
            bool subject_exists = subject_res->next() && subject_res->getInt(1) > 0;

            // 2. Jei dėstytojas arba dalykas neegzistuoja
            if (!teacher_exists) {
                delete con;
                return "Klaida: Destytojas su ID " + std::to_string(teacher_id) + " neegzistuoja.";
            }

            if (!subject_exists) {
                delete con;
                return "Klaida: Dalykas su ID " + std::to_string(subject_id) + " neegzistuoja.";
            }

            // 3. Tikriname, ar egzistuoja ryšys tarp dėstytojo ir dalyko
            std::unique_ptr<sql::PreparedStatement> check_relationship(con->prepareStatement(
                "SELECT COUNT(*) FROM teacher_subjects WHERE teacher_id = ? AND subject_id = ?"));
            check_relationship->setInt(1, teacher_id);
            check_relationship->setInt(2, subject_id);
            std::unique_ptr<sql::ResultSet> relationship_res(check_relationship->executeQuery());
            bool relationship_exists = relationship_res->next() && relationship_res->getInt(1) > 0;

            if (!relationship_exists) {
                delete con;
                return "Klaida: Destytojas su ID " + std::to_string(teacher_id) + " ir dalykas su ID " +
                    std::to_string(subject_id) + " nera priskirti.";
            }

            // 4. Pašaliname ryšį iš teacher_subjects lentelės
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
                "DELETE FROM teacher_subjects WHERE teacher_id = ? AND subject_id = ?"));
            pstmt->setInt(1, teacher_id);
            pstmt->setInt(2, subject_id);

            int affectedRows = pstmt->executeUpdate();
            if (affectedRows > 0) {
                delete con;
                return "success"; // Ryšys buvo sėkmingai pašalintas
            }
            else {
                delete con;
                return "Klaida: Nepavyko pašalinti ryšio.";
            }
        }
        catch (sql::SQLException& e) {
            delete con;
            return "SQL klaida: " + std::string(e.what());
        }
    }

    // Funkcija, kuri grąžina vektorių su dėstytojo ir dalyko informacija
    std::vector<std::tuple<int, std::string, std::string, int, std::string>> getTeacherSubjectInfo(sql::Connection* con) {
        std::vector<std::tuple<int, std::string, std::string, int, std::string>> teacherSubjectInfo;  // Vektorius su tuple..:)

        if (con) {
            try {
                // Atlieka užklausą, kad gautų dėstytojų ir jų priskirtų dalykų informaciją
                unique_ptr<sql::PreparedStatement> pstmt(
                    con->prepareStatement("SELECT t.teacher_id, t.name, t.surname, s.subject_id, s.subject_name "
                        "FROM teachers t "
                        "LEFT JOIN teacher_subjects ts ON t.teacher_id = ts.teacher_id "
                        "LEFT JOIN subjects s ON ts.subject_id = s.subject_id")
                );
                unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                // Užpildo vektorių su dėstytojų ir dalykų informacija
                while (res->next()) {
                    int teacher_id = res->getInt("teacher_id");
                    std::string teacher_name = res->getString("name").c_str();
                    std::string teacher_surname = res->getString("surname").c_str();
                    int subject_id = res->getInt("subject_id");
                    std::string subject_name = res->getString("subject_name").c_str();

                    // Jei dalykas nepriskirtas, įrašome "Nera dalyko" (kažkodėl neveikia UTF-8 koduotė)
                    if (subject_id == 0) {
                        teacherSubjectInfo.push_back(std::make_tuple(teacher_id, teacher_name, teacher_surname, 0, "Nera dalyko"));
                    }
                    else {
                        teacherSubjectInfo.push_back(std::make_tuple(teacher_id, teacher_name, teacher_surname, subject_id, subject_name));
                    }
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida uzklausoje: " << e.what() << std::endl;
            }
        }

        return teacherSubjectInfo;  // Grąžina vektorių su dėstytojo ir dėstomo dalyko informacija
    }

    // Pridedame studentus su grupe.
    std::string addGroupAndStudentToDatabase(MySQLDatabase& db, int group_id, int student_id) {
        // Prisijungimas prie MariaDB
        sql::Connection* con = db.connect();
        if (!con) {
            return "Klaida: Nepavyko prisijungti prie duomenu bazes.";
        }

        try {
            // Patikriname, ar group_id egzistuoja
            std::unique_ptr<sql::PreparedStatement> check_group(con->prepareStatement(
                "SELECT COUNT(*) FROM stud_groups WHERE group_id = ?"));
            check_group->setInt(1, group_id);
            std::unique_ptr<sql::ResultSet> group_res(check_group->executeQuery());
            bool group_exists = group_res->next() && group_res->getInt(1) > 0;

            // Patikriname, ar student_id egzistuoja
            std::unique_ptr<sql::PreparedStatement> check_student(con->prepareStatement(
                "SELECT COUNT(*) FROM students WHERE student_id = ?"));
            check_student->setInt(1, student_id);
            std::unique_ptr<sql::ResultSet> student_res(check_student->executeQuery());
            bool student_exists = student_res->next() && student_res->getInt(1) > 0;

            // Grąžiname klaidą, jei bet kuris neegzistuoja
            if (!group_exists) {
                delete con; // Atlaisviname jungtį
                return "Klaida: Grupe su ID " + std::to_string(group_id) + " neegzistuoja.";
            }

            if (!student_exists) {
                delete con; // Atlaisviname jungtį
                return "Klaida: Studentas su ID " + std::to_string(student_id) + " neegzistuoja.";
            }

            // Tikriname, ar studentas jau priskirtas grupei
            std::unique_ptr<sql::PreparedStatement> pstmt_check_group(con->prepareStatement(
                "SELECT group_id FROM students WHERE student_id = ?"));
            pstmt_check_group->setInt(1, student_id);
            std::unique_ptr<sql::ResultSet> check_group_res(pstmt_check_group->executeQuery());
            if (check_group_res->next() && !check_group_res->isNull(1)) {
                delete con; // Atlaisviname jungtį
                return "Klaida: Studentas su ID " + std::to_string(student_id) + " jau priskirtas kitai grupei.";
            }

            // Priskiriame studentą grupei ir atnaujiname lentelę
            std::unique_ptr<sql::PreparedStatement> insert_group_student(con->prepareStatement(
                "INSERT INTO group_students (group_id, student_id) VALUES (?, ?)"));
            insert_group_student->setInt(1, group_id);
            insert_group_student->setInt(2, student_id);
            insert_group_student->executeUpdate();

            // Atnaujiname students.group_id
            std::unique_ptr<sql::PreparedStatement> update_student_group(con->prepareStatement(
                "UPDATE students SET group_id = ? WHERE student_id = ?"));
            update_student_group->setInt(1, group_id);
            update_student_group->setInt(2, student_id);
            update_student_group->executeUpdate();

            // Priskiriame studentui dalyką (subject)
            std::unique_ptr<sql::PreparedStatement> pstmt_subjects(con->prepareStatement(
                "INSERT INTO students_subjects (student_id, subject_id) "
                "SELECT ?, subject_id FROM group_subjects WHERE group_id = ?"));
            pstmt_subjects->setInt(1, student_id);
            pstmt_subjects->setInt(2, group_id);
            pstmt_subjects->executeUpdate();

            delete con; // Atlaisviname jungtį

            return "Grupe su ID " + std::to_string(group_id) + " buvo priskirta studentui su ID " + std::to_string(student_id) + "!";
        }
        catch (sql::SQLException& e) {
            delete con; // Atlaisviname jungtį klaidos atveju
            return "SQL klaida: " + std::string(e.what());
        }
    }
    // Panaikinti sąryšį tarp studentų ir grupės.
    std::pair<int, std::string> removeGroupAndStudentFromDatabase(MySQLDatabase& db, int group_id, int student_id) {
        sql::Connection* con = db.connect();
        if (!con) {
            return { 500, "Klaida: Nepavyko prisijungti prie duomenų bazės." };
        }

        try {
            // Tikriname, ar egzistuoja grupė
            bool group_exists = false, student_exists = false, student_in_group = false;

            std::unique_ptr<sql::PreparedStatement> pstmt_group(con->prepareStatement(
                "SELECT COUNT(*) FROM stud_groups WHERE group_id = ?"));
            pstmt_group->setInt(1, group_id);
            std::unique_ptr<sql::ResultSet> group_res(pstmt_group->executeQuery());
            group_exists = group_res->next() && group_res->getInt(1) > 0;

            // Tikriname, ar egzistuoja studentas
            std::unique_ptr<sql::PreparedStatement> pstmt_student(con->prepareStatement(
                "SELECT COUNT(*) FROM students WHERE student_id = ?"));
            pstmt_student->setInt(1, student_id);
            std::unique_ptr<sql::ResultSet> student_res(pstmt_student->executeQuery());
            student_exists = student_res->next() && student_res->getInt(1) > 0;

            // Tikriname, ar studentas yra priskirtas grupei
            std::unique_ptr<sql::PreparedStatement> pstmt_in_group(con->prepareStatement(
                "SELECT COUNT(*) FROM group_students WHERE group_id = ? AND student_id = ?"));
            pstmt_in_group->setInt(1, group_id);
            pstmt_in_group->setInt(2, student_id);
            std::unique_ptr<sql::ResultSet> in_group_res(pstmt_in_group->executeQuery());
            student_in_group = in_group_res->next() && in_group_res->getInt(1) > 0;

            // Grąžiname klaidų pranešimus, jei sąlygos nesutampa
            if (!group_exists) {
                return { 400, "Grupe su ID " + std::to_string(group_id) + " neegzistuoja!" };
            }
            if (!student_exists) {
                return { 400, "Studentas su ID " + std::to_string(student_id) + " neegzistuoja!" };
            }
            if (!student_in_group) {
                return { 400, "Studentas su ID " + std::to_string(student_id) + " nera priskirtas grupei su ID " + std::to_string(group_id) + "!" };
            }

            // Jei sąlygos tenkinamos, pašaliname duomenis
            std::unique_ptr<sql::PreparedStatement> pstmt_remove_group(con->prepareStatement(
                "DELETE FROM group_students WHERE group_id = ? AND student_id = ?"));
            pstmt_remove_group->setInt(1, group_id);
            pstmt_remove_group->setInt(2, student_id);
            pstmt_remove_group->executeUpdate();

            // Pašaliname studentą iš students_subjects lentelės
            std::unique_ptr<sql::PreparedStatement> pstmt_remove_subject(con->prepareStatement(
                "DELETE FROM students_subjects WHERE student_id = ? AND subject_id IN "
                "(SELECT subject_id FROM group_subjects WHERE group_id = ?)"));
            pstmt_remove_subject->setInt(1, student_id);
            pstmt_remove_subject->setInt(2, group_id);
            pstmt_remove_subject->executeUpdate();

            // Atnaujiname students lentelę (group_id -> NULL)
            std::unique_ptr<sql::PreparedStatement> pstmt_update_student(con->prepareStatement(
                "UPDATE students SET group_id = NULL WHERE student_id = ?"));
            pstmt_update_student->setInt(1, student_id);
            pstmt_update_student->executeUpdate();

            return { 200, "Studentas su ID " + std::to_string(student_id) + " buvo pasalintas is grupes su ID " + std::to_string(group_id) + "!" };
        }
        catch (sql::SQLException& e) {
            std::cerr << "SQL klaida: " << e.what() << std::endl;
            return { 500, "Klaida salinant is duomenu bazes." };
        }
         {
            delete con;
        }
    }
    // Gauname studentų su grupėmis sąrašą iš DB.
    std::vector<std::tuple<int, std::string, std::string, int, std::string>> getStudentGroupInfoFromDatabase(sql::Connection* con) {
        std::vector<std::tuple<int, std::string, std::string, int, std::string>> studentGroupInfo;  

        if (con) {
            try {
                // Atlieka užklausą, kad gautų studentų ir grupių informaciją
                unique_ptr<sql::PreparedStatement> pstmt(
                    con->prepareStatement("SELECT t.student_id, t.name, t.surname, s.group_id, s.group_name "
                        "FROM students t "
                        "LEFT JOIN group_students ts ON t.student_id = ts.student_id "
                        "LEFT JOIN stud_groups s ON ts.group_id = s.group_id")
                );
                unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                // Užpildo vektorių su studentų ir grupių informacija
                while (res->next()) {
                    int student_id = res->getInt("student_id");
                    std::string student_name = res->getString("name").c_str();
                    std::string student_surname = res->getString("surname").c_str();
                    int group_id = res->getInt("group_id");
                    std::string group_name = res->getString("group_name").c_str();

                    // Įrašo į vektorių tuple su studento ir grupės informacija
                    studentGroupInfo.push_back(std::make_tuple(student_id, student_name, student_surname, group_id, group_name));
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida užklausoje: " << e.what() << std::endl;
            }
        }
        return studentGroupInfo;  // Grąžina vektorių su studentų ir grupių duomenimis
    }

    // Pridėti dėstytoją į duomenų bazę
    std::string addGroupAndSubjectsToDatabase(sql::Connection* con, int group_id, int subject_id) {
        bool group_exists = false, subject_exists = false;

        try {
            // Patikriname, ar egzistuoja grupė
            unique_ptr<sql::PreparedStatement> pstmt_group(con->prepareStatement(
                "SELECT COUNT(*) FROM stud_groups WHERE group_id = ?"));
            pstmt_group->setInt(1, group_id);
            unique_ptr<sql::ResultSet> group_res(pstmt_group->executeQuery());
            group_exists = group_res->next() && group_res->getInt(1) > 0;

            // Patikriname, ar egzistuoja dalykas
            unique_ptr<sql::PreparedStatement> pstmt_subject(con->prepareStatement(
                "SELECT COUNT(*) FROM subjects WHERE subject_id = ?"));
            pstmt_subject->setInt(1, subject_id);
            unique_ptr<sql::ResultSet> subject_res(pstmt_subject->executeQuery());
            subject_exists = subject_res->next() && subject_res->getInt(1) > 0;

            // Patikriname, ar grupė jau turi priskirtą šį dalyką
            unique_ptr<sql::PreparedStatement> pstmt_existing_relation(con->prepareStatement(
                "SELECT COUNT(*) FROM group_subjects WHERE group_id = ? AND subject_id = ?"));
            pstmt_existing_relation->setInt(1, group_id);
            pstmt_existing_relation->setInt(2, subject_id);
            unique_ptr<sql::ResultSet> existing_res(pstmt_existing_relation->executeQuery());
            bool already_assigned = existing_res->next() && existing_res->getInt(1) > 0;

            // Jei jau egzistuoja tas pats ryšys, grąžiname klaidą
            if (already_assigned) {
                return "Sis dalykas jau priskirtas grupei su ID " + std::to_string(group_id);
            }

            // Jei visi patikrinimai praeina, atliekame įrašą į group_subjects
            if (group_exists && subject_exists) {
                unique_ptr<sql::PreparedStatement> pstmt_add(con->prepareStatement(
                    "INSERT INTO group_subjects (group_id, subject_id) VALUES (?, ?)"));
                pstmt_add->setInt(1, group_id);
                pstmt_add->setInt(2, subject_id);
                pstmt_add->executeUpdate();
                return "Dalykas su ID " + std::to_string(subject_id) + " buvo priskirtas grupei su ID " + std::to_string(group_id);
            }
            else {
                if (!group_exists) {
                    return "Grupe su ID " + std::to_string(group_id) + " neegzistuoja.";
                }
                if (!subject_exists) {
                    return "Dalykas su ID " + std::to_string(subject_id) + " neegzistuoja.";
                }
            }
        }
        catch (sql::SQLException& e) {
            std::cerr << "SQL klaida: " << e.what() << std::endl;
            return "Ivyko klaida dirbant su duomenu baze.";
        }

        return "";
    }

    // Funkcija, kuri pašalina grupę ir dalyką iš duomenų bazės
    std::string deleteGroupAndSubjectsFromDatabase(sql::Connection* con, int group_id, int subject_id) {
        bool group_exists = false, subject_exists = false, group_in_subject = false;

        try {
            // Patikriname, ar egzistuoja grupė
            unique_ptr<sql::PreparedStatement> pstmt_group(con->prepareStatement(
                "SELECT COUNT(*) FROM stud_groups WHERE group_id = ?"));
            pstmt_group->setInt(1, group_id);
            unique_ptr<sql::ResultSet> group_res(pstmt_group->executeQuery());
            group_exists = group_res->next() && group_res->getInt(1) > 0;

            // Patikriname, ar egzistuoja dalykas
            unique_ptr<sql::PreparedStatement> pstmt_subject(con->prepareStatement(
                "SELECT COUNT(*) FROM subjects WHERE subject_id = ?"));
            pstmt_subject->setInt(1, subject_id);
            unique_ptr<sql::ResultSet> subject_res(pstmt_subject->executeQuery());
            subject_exists = subject_res->next() && subject_res->getInt(1) > 0;

            // Patikriname, ar grupė turi priskirtą šį dalyką
            unique_ptr<sql::PreparedStatement> pstmt_in_subject(con->prepareStatement(
                "SELECT COUNT(*) FROM group_subjects WHERE group_id = ? AND subject_id = ?"));
            pstmt_in_subject->setInt(1, group_id);
            pstmt_in_subject->setInt(2, subject_id);
            unique_ptr<sql::ResultSet> in_subject_res(pstmt_in_subject->executeQuery());
            group_in_subject = in_subject_res->next() && in_subject_res->getInt(1) > 0;

            // Jei grupė ir dalykas egzistuoja ir grupė yra priskirta šiam dalykui, pašaliname ryšį
            if (group_exists && subject_exists && group_in_subject) {
                unique_ptr<sql::PreparedStatement> pstmt_remove(con->prepareStatement(
                    "DELETE FROM group_subjects WHERE group_id = ? AND subject_id = ?"));
                pstmt_remove->setInt(1, group_id);
                pstmt_remove->setInt(2, subject_id);
                pstmt_remove->executeUpdate();
                return "Grupe su ID " + std::to_string(group_id) + " buvo pasalinta is dalyko su ID " + std::to_string(subject_id);
            }
            else {
                if (!group_exists) {
                    return "Grupe su ID " + std::to_string(group_id) + " neegzistuoja.";
                }
                if (!subject_exists) {
                    return "Dalykas su ID " + std::to_string(subject_id) + " neegzistuoja.";
                }
                if (!group_in_subject) {
                    return "Grupe su ID " + std::to_string(group_id) + " nera priskirtas dalykui su ID " + std::to_string(subject_id) + ".";
                }
            }
        }
        catch (sql::SQLException& e) {
            std::cerr << "SQL klaida: " << e.what() << std::endl;
            return "Įvyko klaida dirbant su duomenų baze.";
        }

        return "";
    }

    // Funkcija, kuri grąžina vektorių su grupių ir dalykų duomenimis
    std::vector<std::tuple<int, std::string, int, std::string>> getGroupSubjects(sql::Connection* con) {
        std::vector<std::tuple<int, std::string, int, std::string>> groupSubjects;

        if (con) {
            try {
                unique_ptr<sql::PreparedStatement> groupSubjectStmt(
                    con->prepareStatement(
                        "SELECT g.group_id, g.group_name, s.subject_id, s.subject_name "
                        "FROM stud_groups g "
                        "LEFT JOIN group_subjects gs ON g.group_id = gs.group_id "
                        "LEFT JOIN subjects s ON gs.subject_id = s.subject_id"
                    )
                );
                unique_ptr<sql::ResultSet> groupSubjectRes(groupSubjectStmt->executeQuery());

                while (groupSubjectRes->next()) {
                    int group_id = groupSubjectRes->getInt("group_id");
                    std::string group_name = groupSubjectRes->getString("group_name").c_str();
                    int subject_id = groupSubjectRes->getInt("subject_id");
                    std::string subject_name = groupSubjectRes->getString("subject_name").c_str();

                    // Jei dalykas nepriskirtas, užrašome "Nėra dalyko"
                    if (subject_id == 0) {
                        subject_name = "Nera dalyko";
                    }

                    // Pridedame į vektorių kaip tuple
                    groupSubjects.push_back(std::make_tuple(group_id, group_name, subject_id, subject_name));
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "SQL klaida: " << e.what() << std::endl;
            }
        }

        return groupSubjects;
    }
    // Gauname visų grupių info iš DB.
    vector<pair<int, string>> getAllGroups(MySQLDatabase& db) {
        std::vector<std::pair<int, std::string>> groups;
        sql::Connection* con = db.connect();
        if (con) {
            try {
                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(
                    "SELECT group_id, group_name FROM `stud_groups`"
                ));
                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                while (res->next()) {
                    int group_id = res->getInt("group_id");
                    std::string group_name = res->getString("group_name").c_str();
                    groups.emplace_back(group_id, group_name);
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida gaunant grupes: " << e.what() << std::endl;
            }
            delete con;
        }
        return groups;
    }
};

MySQLDatabase* db = nullptr;


int main() {
    crow::SimpleApp app;

    // Sukuriame MySQLDatabase objektą
    MySQLDatabase db("127.0.0.1", "root", "Advokatinukas2134", "sys");

    // Pateikti prisijungimo puslapį
    CROW_ROUTE(app, "/")([]() {
        std::string htmlContent = loadHTML("Login.htm");  // Perskaityti Login.htm failą
        crow::response res;
        res.set_header("Content-Type", "text/html");  // Užtikrinti teisingą turinio tipą
        res.body = htmlContent;
        return res;
        });

    // Endpointas prisijungimui
    CROW_ROUTE(app, "/login").methods("POST"_method)([&db](const crow::request& req) {
        std::string body = req.body;
        std::string username;
        std::string password;

        // Suskaidyti POST duomenis
        
        db.parseLoginData(body, username, password);

        // Patikrinti, ar įvestas vartotojas yra teisingas ir gauti vartotojo rolę ir ID
        std::pair<std::string, int> user = db.validateUser(username, password);  // Grąžinsime tiek rolę, tiek ID
        std::string role = user.first;  // Pirmas elementas yra role
        int user_id = user.second;  // Antras elementas yra user_id

        if (!role.empty()) {
            // Prisijungimas sėkmingas, nukreipiame į atitinkamą puslapį pagal vaidmenį
            crow::response res;
            res.code = 302;  // HTTP statusas - peradresavimas

            // Perduodame user_id URL kaip parametru
            if (role == "Destytojas") {
                res.add_header("Location", "/destytojas?teacher_id=" + std::to_string(user_id));  // Nukreipiame į dėstytojo puslapį su teacher_id
            }
            else if (role == "Studentas") {
                res.add_header("Location", "/studentas?student_id=" + std::to_string(user_id));  // Nukreipiame į studento puslapį su student_id
            }
            else if (role == "Administratorius") {
                res.add_header("Location", "/administratorius");
            }

            return res;
        }
        else {
            // Prisijungimas nesėkmingas, grąžiname klaidos pranešimą
            crow::response res(400);
            res.body = "<html><body>Netinkami prisijungimo duomenys.<br></body>"
                "<head><meta http-equiv='refresh' content='2.5; url=/'></head></html>";
            return res;
        }
        });

    // Administratoriaus puslapis
    CROW_ROUTE(app, "/administratorius")([&db]() {
        std::string htmlContent = loadHTML("admin_dashboard.htm");
        crow::response res;
        res.set_header("Content-Type", "text/html");
        res.body = htmlContent;
        return res;
        });
    // Dėstytojo puslapis
    CROW_ROUTE(app, "/destytojas")([&db](const crow::request& req) {
        std::string htmlContent;

        htmlContent += "<button onclick=\"window.location.href='/';\" style='padding: 10px; font-size: 1.2em; position: absolute; top: 10px; right: 10px;'>Atsijungti</button>";

        // Gauti dėstytojo ID iš URL parametrų (pvz., teacher_id=1)
        std::string teacher_id_str = req.url_params.get("teacher_id");
        int teacher_id = 0;

        if (!teacher_id_str.empty()) {
            teacher_id = std::stoi(teacher_id_str);
        }

        sql::Connection* con = db.connect();

        if (con) {
            try {
                std::string name, surname;
                std::vector<std::pair<int, std::string>> subjects;

                // Gauti dėstytojo informaciją
                if (Teacher::getTeacherInfo(teacher_id, con, name, surname)) {
                    htmlContent += "<h2>Sveiki, " + name + " " + surname + "!</h2>";
                    htmlContent += "<h3>Jums priskirti destomi dalykai:</h3>";

                    // Gauti dėstytojui priskirtus dalykus
                    if (Teacher::getSubjects(teacher_id, con, subjects)) {
                        htmlContent += "<table border='1' style='width: 100%;'>";
                        htmlContent += "<tr><th>Dalyko pavadinimas</th><th>Studentu sarasas</th></tr>";

                        // Užpildome lentelę
                        for (const auto& subject : subjects) {
                            htmlContent += "<tr><td>" + subject.second + "</td>";
                            htmlContent += "<td><a href='/subject_students/" + std::to_string(subject.first) + "'>Perziureti studentus</a></td></tr>";
                        }
                        htmlContent += "</table>";
                    }
                    else {
                        htmlContent += "<p>Jums nera priskirtu destomu dalyku.</p>";
                    }
                }
                else {
                    htmlContent += "<h2>Destytojas nerastas.</h2>";
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida uzklausoje: " << e.what() << std::endl;
                htmlContent = "<h2>Klaida uzklausoje: " + std::string(e.what()) + "</h2>";
            }
            delete con;
        }
        else {
            htmlContent = "<h2>Nepavyko prisijungti prie duomenu bazes.</h2>";
        }

        // HTML pabaiga
        htmlContent += "</body></html>";

        // Grąžiname HTML turinį su UTF-8 antrašte
        crow::response res;
        res.set_header("Content-Type", "text/html; charset=utf-8"); // Neveikia UTF-8 koduotė.....
        res.body = htmlContent;
        return res;
        });
    // Studento puslapis
    CROW_ROUTE(app, "/studentas")([&db](const crow::request& req) {
        std::string htmlContent;

        // Gauti studento ID iš URL parametrų (pvz., ?student_id=1)
        std::string student_id_str = req.url_params.get("student_id");
        int student_id = 0;  // Numatytoji reikšmė, jei parametras nėra pateiktas

        if (!student_id_str.empty()) {
            student_id = std::stoi(student_id_str);  // Jei parametras yra, konvertuoti į int
        }

        sql::Connection* con = db.connect();

        if (con) {
            try {
                std::string name, surname;
                std::vector<std::pair<std::string, std::string>> subjects;
                htmlContent += "<button onclick=\"window.location.href='/';\" style='padding: 10px; font-size: 1.2em; position: absolute; top: 10px; right: 10px;'>Atsijungti</button>";

                // Naudojame Student klasės metodą gauti informacijai apie studentą
                if (Student::getStudentData(student_id, con, name, surname, subjects)) {
                    // Pasisveikinimas su studentu
                    htmlContent += "<h2>Sveiki, " + name + " " + surname + "!</h2>";
                    htmlContent += "<h3>Destomu dalyku lentele su pazymiais:</h3>";

                    // Lentelės pradžia
                    htmlContent += "<table border='1' style='width: 100%;'>";
                    htmlContent += "<tr><th>Dalyko pavadinimas</th><th>Pazymys</th></tr>";

                    // Užpildome lentelę su dalykais ir pažymiais
                    if (subjects.empty()) {
                        htmlContent += "<tr><td colspan='2'>Studentas neturi priskirtu dalyku.</td></tr>";
                    }
                    else {
                        for (const auto& subject : subjects) {
                            htmlContent += "<tr><td>" + subject.first + "</td><td>" + subject.second + "</td></tr>";
                        }
                    }

                    htmlContent += "</table>";
                }
                else {
                    htmlContent += "<h2>Studentas nerastas.</h2>";
                }
            }
            catch (sql::SQLException& e) {
                std::cerr << "Klaida uzklausoje: " << e.what() << std::endl;
                htmlContent = "<h2>Ivyko klaida apdorojant uzklausa.</h2>";
            }
            delete con;
        }
        else {
            htmlContent = "<h2>Nepavyko prisijungti prie duomenu bazes.</h2>";
        }

        // Grąžiname HTML turinį
        crow::response res;
        res.set_header("Content-Type", "text/html; charset=utf-8");
        res.body = htmlContent;
        return res;
        });

    // Studentų sąrašo (pagal dėstomus dalykus) maršrutas
    CROW_ROUTE(app, "/subject_students/<int>").methods("GET"_method)([&db](const crow::request& req, int subject_id) {
        std::string htmlContent = loadHTML("subject_students.html");
        if (req.method == crow::HTTPMethod::GET) {
            sql::Connection* con = db.connect();
            if (con) {
                try {
                    std::string subject_name;
                    std::string students_html;

                    // Gauti dalyko informaciją
                    if (Teacher::getSubjectInfo(subject_id, con, subject_name)) {
                        // Pakeičiame {{subject_name}} vietas su tikru pavadinimu
                        size_t pos = 0;
                        while ((pos = htmlContent.find("{{subject_name}}", pos)) != std::string::npos) {
                            htmlContent.replace(pos, 16, subject_name);
                            pos += subject_name.length(); // pereina prie kitos vietos
                        }

                        // Pakeičiame {{subject_id}} vietas su tikru subject_id
                        pos = 0;
                        while ((pos = htmlContent.find("{{subject_id}}", pos)) != std::string::npos) {
                            htmlContent.replace(pos, 14, std::to_string(subject_id));
                            pos += std::to_string(subject_id).length(); // pereina prie kitos vietos
                        }

                        // Gauti studentus susijusius su šiuo dalyku
                        if (Teacher::getStudentsForSubject(subject_id, con, students_html)) {
                            // Pakeičiame {{students}} su studentų sąrašu
                            size_t pos = htmlContent.find("{{students}}");
                            if (pos != std::string::npos) {
                                htmlContent.replace(pos, 12, students_html);
                            }
                        }
                        else {
                            // Jei nėra studentų
                            size_t pos = htmlContent.find("{{students}}");
                            if (pos != std::string::npos) {
                                htmlContent.replace(pos, 12, "<tr><td colspan='4'>Nera studentu siam dalykui.</td></tr>");
                            }
                        }
                    }
                    else {
                        htmlContent += "<p>Dalykas nerastas.</p>";
                    }
                }
                catch (sql::SQLException& e) {
                    std::cerr << "SQL klaida: " << e.what() << std::endl;
                    htmlContent = "<h2>SQL klaida: " + std::string(e.what()) + "</h2>";
                }
                delete con;
            }

            crow::response res;
            res.set_header("Content-Type", "text/html");
            res.body = htmlContent;
            return res;
        }

        return crow::response(404, "Nerasta.");
        });
    // Maršrutas pažymio pridėjimui
    CROW_ROUTE(app, "/add_grade")
        .methods("POST"_method)([&db](const crow::request& req) {
        try {
            auto json = crow::json::load(req.body);
            if (!json) {
                return crow::response(400, "{\"status\": \"error\", \"message\": \"Invalid JSON object\"}");
            }

            int student_id = json["student_id"].i();
            int grade = json["grade"].i();
            int subject_id = json["subject_id"].i();

            sql::Connection* con = db.connect();
            if (con) {
                try {
                    // Patikriname, ar studentas egzistuoja
                    if (!Teacher::checkStudentExistence(student_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Tokio studento nera.\"}");
                    }

                    // Patikriname, ar studentas yra priskirtas tam tikram dalykui
                    if (!Teacher::checkStudentSubjectAssignment(student_id, subject_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Studentas nera priskirtas siam dalykui.\"}");
                    }

                    // Patikriname, ar studentas jau turi pažymį šiam dalykui
                    if (Teacher::checkStudentGradeExistence(student_id, subject_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Studentas jau turi pazymi siam dalykui.\"}");
                    }

                    // Pridedame naują pažymį
                    if (Teacher::addGrade(student_id, subject_id, grade, con)) {
                        return crow::response(200, "{\"status\": \"success\", \"message\": \"Pazymys sekmingai pridetas.\"}");
                    }
                    else {
                        return crow::response(500, "{\"status\": \"error\", \"message\": \"Nepavyko prideti pazymio.\"}");
                    }
                }
                catch (sql::SQLException& e) {
                    std::cerr << "SQL klaida: " << e.what() << std::endl;
                    return crow::response(500, "{\"status\": \"error\", \"message\": \"Vidine klaida.\"}");
                }
                delete con;
            }

            return crow::response(500, "{\"status\": \"error\", \"message\": \"Prisijungimo klaida prie duomenu bazes.\"}");
        }
        catch (const std::exception& e) {
            return crow::response(400, "{\"status\": \"error\", \"message\": \"Invalid JSON object\"}");
        }
            });

// Maršrutas pažymio ištrynimui pagal studento ID
    CROW_ROUTE(app, "/delete_grade/<int>") // Maršrutas priima <subject_id>
        .methods("POST"_method)([&db](const crow::request& req, int subject_id) {
        try {
            auto json = crow::json::load(req.body);
            if (!json) {
                return crow::response(400, "{\"status\": \"error\", \"message\": \"Invalid JSON object\"}");
            }

            auto student_id = json["student_id"].i();

            sql::Connection* con = db.connect();
            if (con) {
                try {
                    // Patikriname, ar studentas egzistuoja
                    if (!Teacher::checkStudentExistence(student_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Tokio studento nera.\"}");
                    }

                    // Patikriname, ar studentas yra priskirtas tam tikram dalykui
                    if (!Teacher::checkStudentSubjectAssignment(student_id, subject_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Studentas nera priskirtas siam dalykui.\"}");
                    }

                    // Patikriname, ar studentas turi pažymį šiam dalykui
                    if (!Teacher::checkStudentGradeExistence(student_id, subject_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Studentas neturi pazymio siam dalykui.\"}");
                    }

                    // Ištriname pažymį
                    if (Teacher::deleteGrade(student_id, subject_id, con)) {
                        return crow::response(200, "{\"status\": \"success\", \"message\": \"Pazymys sekmingai istrintas.\"}");
                    }
                    else {
                        return crow::response(500, "{\"status\": \"error\", \"message\": \"Nepavyko istrinti pazymio.\"}");
                    }
                }
                catch (sql::SQLException& e) {
                    std::cerr << "SQL klaida: " << e.what() << std::endl;
                    return crow::response(500, "{\"status\": \"error\", \"message\": \"Vidine klaida.\"}");
                }
                delete con;
            }

            return crow::response(500, "{\"status\": \"error\", \"message\": \"Prisijungimo klaida prie duomenu bazes.\"}");
        }
        catch (const std::exception& e) {
            return crow::response(400, "{\"status\": \"error\", \"message\": \"Invalid JSON object\"}");
        }
            });
    // Pažymio koregavimo maršrutas
    CROW_ROUTE(app, "/update_grade").methods("POST"_method)([&db](const crow::request& req) {
        try {
            auto json = crow::json::load(req.body);
            if (!json) {
                return crow::response(400, "{\"status\": \"error\", \"message\": \"Invalid JSON object\"}");
            }

            int student_id = json["student_id"].i();
            int new_grade = json["grade"].i();
            int subject_id = json["subject_id"].i();

            sql::Connection* con = db.connect();
            if (con) {
                try {
                    // Patikriname, ar studentas egzistuoja
                    if (!Teacher::checkStudentExistence(student_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Tokio studento nera.\"}");
                    }

                    // Patikriname, ar studentas yra priskirtas tam tikram dalykui
                    if (!Teacher::checkStudentSubjectAssignment(student_id, subject_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Studentas nera priskirtas siam dalykui.\"}");
                    }

                    // Patikriname, ar studentas turi pažymį šiam dalykui
                    if (!Teacher::checkStudentGradeExistence(student_id, subject_id, con)) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Studentas neturi pazymio siam dalykui.\"}");
                    }

                    // Patikriname, ar naujas pažymys nesutampa su esamu
                    int current_grade = Teacher::getCurrentGrade(student_id, subject_id, con);
                    if (current_grade == new_grade) {
                        return crow::response(400, "{\"status\": \"error\", \"message\": \"Naujas pazymys yra toks pats kaip senas.\"}");
                    }

                    // Atnaujiname pažymį
                    if (Teacher::updateGrade(student_id, new_grade, subject_id, con)) {
                        return crow::response(200, "{\"status\": \"success\", \"message\": \"Pazymys sekmingai atnaujintas.\"}");
                    }
                    else {
                        return crow::response(500, "{\"status\": \"error\", \"message\": \"Nepavyko atnaujinti pazymio.\"}");
                    }
                }
                catch (sql::SQLException& e) {
                    std::cerr << "SQL klaida: " << e.what() << std::endl;
                    return crow::response(500, "{\"status\": \"error\", \"message\": \"Vidine klaida.\"}");
                }
                delete con;
            }

            return crow::response(500, "{\"status\": \"error\", \"message\": \"Prisijungimo klaida prie duomenu bazes.\"}");
        }
        catch (const std::exception& e) {
            return crow::response(400, "{\"status\": \"error\", \"message\": \"Invalid JSON object\"}");
        }
        });
    // Maršrutas grupės ir dėstomo dalyko ištrinimui
    CROW_ROUTE(app, "/delete_groupandsubjects").methods("POST"_method)
        ([&db](const crow::request& req) {
        std::string body = req.body;
        int group_id, subject_id;
        try {
            // Parsiname parametrus iš užklausos
            group_id = std::stoi(body.substr(body.find("group_id=") + 9));
            subject_id = std::stoi(body.substr(body.find("subject_id=") + 11));
        }
        catch (...) {
            return crow::response(400, "Blogai pateikti duomenys.");
        }

        sql::Connection* con = db.connect();
        Administrator admin(1, "Test", "Testas");
        std::string result = admin.deleteGroupAndSubjectsFromDatabase(con, group_id, subject_id);

        // Jei atsakymas turi klaidą
        if (result.find("Grupe su ID") != std::string::npos ||
            result.find("Dalykas su ID") != std::string::npos ||
            result.find("Grupe su ID") != std::string::npos) {
            return crow::response(200, "<html><head><meta http-equiv='refresh' content='2.5; url=/groupandsubjects'></head>"
                "<body>" + result + "<br></body></html>");
        }

        // Jei pašalinimas sėkmingas:
        return crow::response(200, "<html><head><meta http-equiv='refresh' content='2.5; url=/groupandsubjects'></head>"
            "<body>" + result + "<br></body></html>");
            });
    // Grupės su dėstomų dalykų pridėjimas (maršrutas)
    CROW_ROUTE(app, "/add_groupandsubjects").methods("POST"_method)
        ([&db](const crow::request& req) {
        std::string body = req.body;
        int group_id, subject_id;

        try {
            // Parsiname parametrus iš užklausos
            group_id = std::stoi(body.substr(body.find("group_id=") + 9));
            subject_id = std::stoi(body.substr(body.find("subject_id=") + 11));
        }
        catch (...) {
            return crow::response(400, "Blogai pateikti duomenys.");
        }

        sql::Connection* con = db.connect();
        Administrator admin(1, "Test", "Testas");
        std::string result = admin.addGroupAndSubjectsToDatabase(con, group_id, subject_id);

        // Patikriname, ar rezultatas nėra tuščias klaidos pranešimui
        if (result.find("Šis dalykas jau priskirtas") != std::string::npos ||
            result.find("Grupe su ID") != std::string::npos ||
            result.find("Dalykas su ID") != std::string::npos) {
            return crow::response(400, "<html><body>" + result + "<br></body>"
                "<head><meta http-equiv='refresh' content='2.5; url=/groupandsubjects'></head></html>");
        }

        // Jei viskas gerai, grąžiname sėkmės pranešimą
        return crow::response(200, "<html><body>" + result + "!</body>"
            "<head><meta http-equiv='refresh' content='2.5; url=/groupandsubjects'></head></html>");
            });
    // pgr. Langas grupių ir dėstomų dalykų
    CROW_ROUTE(app, "/groupandsubjects")
        ([&db]() {
        sql::Connection* con = db.connect();
        std::string htmlContent = "<div style='display: flex; flex-direction: row;'>";

        // Kairėje - visų grupių ir dalykų lentelės
        htmlContent += "<h2></h2>";
        htmlContent += "<div style='width: 50%; padding: 10px;'>";
        htmlContent += "<button onclick=\"window.location.href='/administratorius';\" style='padding: 10px; font-size: 1.2em;'>Atgal</button>";

        // 1. Lentelė - Grupės
        htmlContent += "<h2>Grupes</h2>";
        htmlContent += "<table border='1' style='width: 100%; margin-bottom: 20px;'>";
        htmlContent += "<tr><th>ID</th><th>Grupes pavadinimas</th></tr>";

        // Naudojame getAllGroupsFromDatabase funkciją
        Administrator admin(1, "Test", "Testas");
        std::vector<std::tuple<int, std::string>> groups = admin.getAllGroupsFromDatabase(con);
        for (const auto& group : groups) {
            int group_id = std::get<0>(group);
            std::string group_name = std::get<1>(group);
            htmlContent += "<tr><td>" + std::to_string(group_id) + "</td><td>" + group_name + "</td></tr>";
        }
        htmlContent += "</table>";

        // 2. Lentelė - Dalykų sąrašas
        htmlContent += "<h2>Destomi Dalykai</h2>";
        htmlContent += "<table border='1' style='width: 100%; margin-bottom: 20px;'>";
        htmlContent += "<tr><th>ID</th><th>Pavadinimas</th></tr>";

        // Naudojame getSubjectsFromDatabase funkciją
        std::vector<std::tuple<int, std::string>> subjects = admin.getSubjectsFromDatabase(con);
        for (const auto& subject : subjects) {
            int subject_id = std::get<0>(subject);
            std::string subject_name = std::get<1>(subject);
            htmlContent += "<tr><td>" + std::to_string(subject_id) + "</td><td>" + subject_name + "</td></tr>";
        }
        htmlContent += "</table>";

        // 3. Lentelė - Grupės su priskirtais dalykais
        htmlContent += "<h2>Grupes su priskirtais dalykais</h2>";
        htmlContent += "<table border='1' style='width: 100%;'>";
        htmlContent += "<tr><th>Grupes ID</th><th>Grupes pavadinimas</th><th>Dalyko ID</th><th>Destomo dalyko pavadinimas</th></tr>";

        // Naudojame getGroupSubjects funkciją
        std::vector<std::tuple<int, std::string, int, std::string>> groupSubjects = admin.getGroupSubjects(con);
        for (const auto& groupSubject : groupSubjects) {
            int group_id = std::get<0>(groupSubject);
            std::string group_name = std::get<1>(groupSubject);
            int subject_id = std::get<2>(groupSubject);
            std::string subject_name = std::get<3>(groupSubject);

            // Jei dalykas nepriskirtas, rodyti "Nėra dalyko"
            if (subject_id == 0) {
                htmlContent += "<tr><td>" + std::to_string(group_id) + "</td><td>" + group_name + "</td><td colspan='2'>Nera destomo dalyko</td></tr>";
            }
            else {
                htmlContent += "<tr><td>" + std::to_string(group_id) + "</td><td>" + group_name + "</td><td>" + std::to_string(subject_id) + "</td><td>" + subject_name + "</td></tr>";
            }
        }
        htmlContent += "</table>";

        htmlContent += "</div>";  // Užbaigiame kairės pusės div'ą

        // Dešinėje - forma grupės ir dalyko priskyrimui
        htmlContent += "<div style='width: 50%; padding: 10px;'>";
        htmlContent += "<h1>Priskirti grupe destomui dalykui</h1>";

        // Forma priskyrimo
        htmlContent += "<form action='/add_groupandsubjects' method='POST'>";
        htmlContent += "<label for='group_id'>Grupes ID:</label><br>";
        htmlContent += "<input type='number' name='group_id' required><br><br>";
        htmlContent += "<label for='subject_id'>Dalyko ID:</label><br>";
        htmlContent += "<input type='number' name='subject_id' required><br><br>";
        htmlContent += "<button type='submit'>Priskirti grupe destomui dalykui</button>";
        htmlContent += "</form>";

        htmlContent += "<h1>Pasalinti destoma dalyka is grupes</h1>";

        // Forma pašalinimo
        htmlContent += "<form action='/delete_groupandsubjects' method='POST'>";
        htmlContent += "<label for='group_id'>Grupes ID:</label><br>";
        htmlContent += "<input type='number' name='group_id' required><br><br>";
        htmlContent += "<label for='subject_id'>Dalyko ID:</label><br>";
        htmlContent += "<input type='number' name='subject_id' required><br><br>";
        htmlContent += "<button type='submit'>Pasalinti destoma dalyka is grupes</button>";
        htmlContent += "</form>";

        htmlContent += "</div>";  // Užbaigiame dešinės pusės div'ą
        htmlContent += "</div>";  // Užbaigiame pagrindinį div'ą

        // Grąžiname HTML turinį
        crow::response res;
        res.set_header("Content-Type", "text/html");
        res.body = htmlContent;
        return res;
            });


    // Trinimo maršrutas su student_id egzistavimo patikrinimu
      CROW_ROUTE(app, "/delete_teacherandsubjects").methods("POST"_method)
                ([&db](const crow::request& req) {
                std::string body = req.body;
                int teacher_id, subject_id;
                try {
                    teacher_id = std::stoi(body.substr(body.find("teacher_id=") + 11));
                    subject_id = std::stoi(body.substr(body.find("subject_id=") + 11));
                }
                catch (...) {
                    return crow::response(400, "Blogai pateikti duomenys.");
                }

                // Naudojame funkciją, kad pašalintume dėstytoją iš dalyko
                Administrator admin(1, "Test", "Testas");
                std::string result = admin.removeTeacherAndSubjectFromDatabase(db, teacher_id, subject_id);

                if (result == "success") {
                    return crow::response(200, "<html><head><meta http-equiv='refresh' content='2.5; url=/teacherandsubjects'></head>"
                        "<body>Destytojas su ID " + std::to_string(teacher_id) + " buvo pasalintas is destomo dalyko su ID " +
                        std::to_string(subject_id) + "!</body></html>");
                }
                else {
                    return crow::response(400, result); // Grąžiname klaidos pranešimą
                }
                    });
            // Pridėti dėstytoją prie dėstomo dalyko (maršrutas)
    CROW_ROUTE(app, "/add_teacherandsubjects").methods("POST"_method)
        ([&db](const crow::request& req) {
        std::string body = req.body;
        int teacher_id, subject_id;

        try {
            // Parsiname parametrus iš užklausos
            teacher_id = std::stoi(body.substr(body.find("teacher_id=") + 11));
            subject_id = std::stoi(body.substr(body.find("subject_id=") + 11));
        }
        catch (...) {
            return crow::response(400, "Blogai pateikti duomenys.");
        }

        // Naudojame bendrą funkciją, kad išvengtume dubliavimo
        Administrator admin(1, "Test", "Testas");
        std::string result = admin.addTeacherAndSubjectToDatabase(db, teacher_id, subject_id);

        if (result == "success") {
            return crow::response(200, "<html><body>Destytojas su ID " + std::to_string(teacher_id) +
                " buvo priskirtas destomui dalykui su ID " + std::to_string(subject_id) + "!</body>"
                "<head><meta http-equiv='refresh' content='2.5; url=/teacherandsubjects'></head></html>");
        }
        else {
            return crow::response(400, result); // Grąžiname klaidos pranešimą
        }
            });
    // pgr. dėstytojų ir dalykų lango maršrutas
    CROW_ROUTE(app, "/teacherandsubjects")
        ([&db]() {
        sql::Connection* con = db.connect();
        std::string htmlContent = "<div style='display: flex; flex-direction: row;'>";

        // Kairėje - visų dėstytojų, dalykų ir dėstytojų su jų dalykais lentelės
        htmlContent += "<h2></h2>";
        htmlContent += "<div style='width: 50%; padding: 10px;'>";
        htmlContent += "<button onclick=\"window.location.href='/administratorius';\" style='padding: 10px; font-size: 1.2em;'>Atgal</button>";

        // 1. Lentelė - Dėstytojai
        htmlContent += "<h2>Destytojai</h2>";
        htmlContent += "<table border='1' style='width: 100%; margin-bottom: 20px;'>";
        htmlContent += "<tr><th>ID</th><th>Vardas</th><th>Pavarde</th></tr>";

        // Naudojame anksčiau aprašytą funkciją getAllTeachers
        Administrator admin(1, "Test", "Testas");
        std::vector<std::tuple<int, std::string, std::string>> teachers = admin.getAllTeachers(con);
        for (const auto& teacher : teachers) {
            int teacher_id = std::get<0>(teacher);
            std::string teacher_name = std::get<1>(teacher);
            std::string teacher_surname = std::get<2>(teacher);
            htmlContent += "<tr><td>" + std::to_string(teacher_id) + "</td><td>" + teacher_name + "</td><td>" + teacher_surname + "</td></tr>";
        }
        htmlContent += "</table>";

        // 2. Lentelė - Dalykų sąrašas
        htmlContent += "<h2>Destomi dalykai</h2>";
        htmlContent += "<table border='1' style='width: 100%; margin-bottom: 20px;'>";
        htmlContent += "<tr><th>ID</th><th>Pavadinimas</th></tr>";

        // Naudojame funkciją getSubjectsFromDatabase
        std::vector<std::tuple<int, std::string>> subjects = admin.getSubjectsFromDatabase(con);
        for (const auto& subject : subjects) {
            int subject_id = std::get<0>(subject);
            std::string subject_name = std::get<1>(subject);
            htmlContent += "<tr><td>" + std::to_string(subject_id) + "</td><td>" + subject_name + "</td></tr>";
        }
        htmlContent += "</table>";

        // 3. Lentelė - Dėstytojai su priskirtais dalykais
        htmlContent += "<h2>Destytojai su priskirtais destomais dalykais</h2>";
        htmlContent += "<table border='1' style='width: 100%;'>";
        htmlContent += "<tr><th>Destytojo ID</th><th>Vardas ir Pavarde</th><th>Destomo dalyko ID</th><th>Destomo dalyko pavadinimas</th></tr>";

        // Naudojame funkciją getTeacherSubjectInfo
        std::vector<std::tuple<int, std::string, std::string, int, std::string>> teacherSubjectInfo = admin.getTeacherSubjectInfo(con);
        for (const auto& item : teacherSubjectInfo) {
            int teacher_id = std::get<0>(item);
            std::string teacher_name = std::get<1>(item);
            std::string teacher_surname = std::get<2>(item);
            int subject_id = std::get<3>(item);
            std::string subject_name = std::get<4>(item);

            // Jei dalykas nepriskirtas, rodyti "Nėra dalyko"
            if (subject_id == 0) {
                htmlContent += "<tr><td>" + std::to_string(teacher_id) + "</td><td>" + teacher_name + " " + teacher_surname + "</td><td colspan='2'>Nera destomo dalyko</td></tr>";
            }
            else {
                htmlContent += "<tr><td>" + std::to_string(teacher_id) + "</td><td>" + teacher_name + " " + teacher_surname + "</td><td>" + std::to_string(subject_id) + "</td><td>" + subject_name + "</td></tr>";
            }
        }
        htmlContent += "</table>";

        htmlContent += "</div>";  // Užbaigiamas kairės pusės div'as

        // Dešinėje - forma dėstytojo pridėjimui prie dalyko
        htmlContent += "<div style='width: 50%; padding: 10px;'>";
        htmlContent += "<h1>Priskirti destytoja destomui dalykui</h1>";

        // Forma priskyrimo
        htmlContent += "<form action='/add_teacherandsubjects' method='POST'>";
        htmlContent += "<label for='teacher_id'>Destytojo ID:</label><br>";
        htmlContent += "<input type='number' name='teacher_id' required><br><br>";
        htmlContent += "<label for='subject_id'>Dalyko ID:</label><br>";
        htmlContent += "<input type='number' name='subject_id' required><br><br>";
        htmlContent += "<button type='submit'>Priskirti destytoja destomui dalykui</button>";
        htmlContent += "</form>";

        htmlContent += "<h1>Pasalinti destytoja is destomo dalyko</h1>";

        // Forma pašalinimo
        htmlContent += "<form action='/delete_teacherandsubjects' method='POST'>";
        htmlContent += "<label for='teacher_id'>Destytojo ID:</label><br>";
        htmlContent += "<input type='number' name='teacher_id' required><br><br>";
        htmlContent += "<label for='subject_id'>Dalyko ID:</label><br>";
        htmlContent += "<input type='number' name='subject_id' required><br><br>";
        htmlContent += "<button type='submit'>Pasalinti destytoja is destomo dalyko</button>";
        htmlContent += "</form>";

        htmlContent += "</div>";  // Užbaigiamas dešinės pusės div'as
        htmlContent += "</div>";  // Užbaigiamas pagrindinis div'as

        // Grąžiname HTML turinį
        crow::response res;
        res.set_header("Content-Type", "text/html");
        res.body = htmlContent;
        return res;
            });
    // maršrutas ištrinti grupe ir studentą.
            CROW_ROUTE(app, "/delete_groupandstudents").methods("POST"_method)
                ([&db](const crow::request& req) {
                std::string body = req.body;
                int group_id, student_id;
                try {
                    group_id = std::stoi(body.substr(body.find("group_id=") + 9));
                    student_id = std::stoi(body.substr(body.find("student_id=") + 11));
                }
                catch (...) {
                    return crow::response(400, "Blogai pateikti duomenys.");
                }

                // Kvietimas į duomenų bazės funkciją
                Administrator admin(1, "Test", "Testas");
                auto result = admin.removeGroupAndStudentFromDatabase(db, group_id, student_id);

                // Generuojamas HTML atsakymas
                if (result.first == 200) {
                    return crow::response(200, "<html><head><meta http-equiv='refresh' content='2.5; url=/groupandstudents'></head>"
                        "<body>" + result.second + "<br></body></html>");
                }
                else {
                    return crow::response(result.first, "<html><head><meta http-equiv='refresh' content='2.5; url=/groupandstudents'></head>"
                        "<body>" + result.second + "<br></body></html>");
                }
                    });
            // pridėti grupę su studentu-ais.
            CROW_ROUTE(app, "/add_groupandstudents").methods("POST"_method)
                ([&db](const crow::request& req) {
                std::string body = req.body;
                int group_id, student_id;

                try {
                    // Parsiname parametrus iš užklausos
                    group_id = std::stoi(body.substr(body.find("group_id=") + 9));
                    student_id = std::stoi(body.substr(body.find("student_id=") + 11));
                }
                catch (...) {
                    return crow::response(400, "Blogai pateikti duomenys.");
                }

                // Kviečiame addGroupAndStudentToDatabase funkciją
                Administrator admin(1, "Test", "Testas");
                std::string result = admin.addGroupAndStudentToDatabase(db, group_id, student_id);

                // Grąžiname atsakymą priklausomai nuo rezultato
                if (result.find("Klaida") != std::string::npos) {
                    return crow::response(400, "<html><body>" + result + "<br></body>"
                        "<head><meta http-equiv='refresh' content='2.5; url=/groupandstudents'></head></html>");
                }
                else {
                    return crow::response(200, "<html><body>" + result + "<br></body>"
                        "<head><meta http-equiv='refresh' content='2.5; url=/groupandstudents'></head></html>");
                }
                    });
            // pgr. grupių ir studentų langas (maršrutas)
            CROW_ROUTE(app, "/groupandstudents")
                ([&db]() {
                sql::Connection* con = db.connect();
                std::string htmlContent = "<div style='display: flex; flex-direction: row;'>";

                // Kairėje - visų dėstytojų, dalykų ir dėstytojų su jų dalykais lentelės
                htmlContent += "<h2></h2>";
                htmlContent += "<div style='width: 50%; padding: 10px;'>";
                htmlContent += "<button onclick=\"window.location.href='/administratorius';\" style='padding: 10px; font-size: 1.2em;'>Atgal</button>";

                // 1. Lentelė - Grupės
                htmlContent += "<h2>Grupes</h2>";
                htmlContent += "<table border='1' style='width: 100%; margin-bottom: 20px;'>";
                htmlContent += "<tr><th>ID</th><th>Grupes pavadinimas</th></tr>";

                // Paimame grupių informaciją
                Administrator admin(1, "Test", "Testas"); // Sukuriame administratorių, čia tik pavyzdys
                std::vector<std::tuple<int, std::string>> groups = admin.getAllGroupsFromDatabase(con);
                for (const auto& group : groups) {
                    int group_id = std::get<0>(group);
                    std::string group_name = std::get<1>(group);
                    htmlContent += "<tr><td>" + std::to_string(group_id) + "</td><td>" + group_name + "</td></tr>";
                }
                htmlContent += "</table>";

                // 2. Lentelė - Studentai
                htmlContent += "<h2>Studentai</h2>";
                htmlContent += "<table border='1' style='width: 100%; margin-bottom: 20px;'>";
                htmlContent += "<tr><th>ID</th><th>Vardas</th><th>Pavarde</th></tr>";

                // Paimame studentų informaciją
                
                std::vector<std::tuple<int, std::string, std::string>> students = admin.getAllStudents(con);
                for (const auto& student : students) {
                    int student_id = std::get<0>(student);
                    std::string student_name = std::get<1>(student);
                    std::string student_surname = std::get<2>(student);
                    htmlContent += "<tr><td>" + std::to_string(student_id) + "</td><td>" + student_name + "</td><td>" + student_surname + "</td></tr>";
                }
                htmlContent += "</table>";

                // 3. Lentelė - Studentai su paskirtomis grupėmis
                htmlContent += "<h2>Studentai su paskirta grupe</h2>";
                htmlContent += "<table border='1' style='width: 100%;'>";
                htmlContent += "<tr><th>Studento ID</th><th>Vardas ir Pavarde</th><th>Grupes ID</th><th>Grupes pavadinimas</th></tr>";

                // Paimame studentų ir grupių informaciją
                
                std::vector<std::tuple<int, std::string, std::string, int, std::string>> studentGroupInfo = admin.getStudentGroupInfoFromDatabase(con);
                for (const auto& entry : studentGroupInfo) {
                    int student_id = std::get<0>(entry);
                    std::string student_name = std::get<1>(entry);
                    std::string student_surname = std::get<2>(entry);
                    int group_id = std::get<3>(entry);
                    std::string group_name = std::get<4>(entry);

                    // Jei grupė nepriskirta, rodyti "Nėra grupės"
                    if (group_id == 0) {
                        htmlContent += "<tr><td>" + std::to_string(student_id) + "</td><td>" + student_name + " " + student_surname + "</td><td colspan='2'>Nera grupės</td></tr>";
                    }
                    else {
                        htmlContent += "<tr><td>" + std::to_string(student_id) + "</td><td>" + student_name + " " + student_surname + "</td><td>" + std::to_string(group_id) + "</td><td>" + group_name + "</td></tr>";
                    }
                }
                htmlContent += "</table>";

                htmlContent += "</div>";  // Užbaigiamas kairės pusės div'as

                // Dešinėje - forma dėstytojo pridėjimui prie dalyko
                htmlContent += "<div style='width: 50%; padding: 10px;'>";
                htmlContent += "<h1>Priskirti studenta grupei</h1>";

                // Forma priskyrimo
                htmlContent += "<form action='/add_groupandstudents' method='POST'>";
                htmlContent += "<label for='student_id'>Studento ID:</label><br>";
                htmlContent += "<input type='number' name='student_id' required><br><br>";
                htmlContent += "<label for='group_id'>Grupes ID:</label><br>";
                htmlContent += "<input type='number' name='group_id' required><br><br>";
                htmlContent += "<button type='submit'>Priskirti studenta grupei</button>";
                htmlContent += "</form>";

                htmlContent += "<h1>Pasalinti studenta is grupes</h1>";

                // Forma pašalinimo
                htmlContent += "<form action='/delete_groupandstudents' method='POST'>";
                htmlContent += "<label for='student_id'>Studento ID:</label><br>";
                htmlContent += "<input type='number' name='student_id' required><br><br>";
                htmlContent += "<label for='group_id'>Grupes ID:</label><br>";
                htmlContent += "<input type='number' name='group_id' required><br><br>";
                htmlContent += "<button type='submit'>Pasalinti studenta is grupes</button>";
                htmlContent += "</form>";

                htmlContent += "</div>";  // Užbaigiamas dešinės pusės div'as

                htmlContent += "</div>";  // Užbaigiamas pagrindinis div'as

                // Grąžiname HTML turinį
                crow::response res;
                res.set_header("Content-Type", "text/html");
                res.body = htmlContent;
                return res;
                    });
                    // Maršrutas dėstomo dalyko ištrinimui.
                    CROW_ROUTE(app, "/delete_subject").methods("POST"_method)
                        ([&db](const crow::request& req) {
                        std::string body = req.body;
                        std::string subject_id_str;
                        std::size_t pos = body.find("subject_id=");

                        if (pos != std::string::npos) {
                            subject_id_str = body.substr(pos + 11);
                        }

                        int subject_id = std::stoi(subject_id_str);

                        // Naudojame Administrator klasę ir jos metodą, kad pašalintume dalyką
                        Administrator admin(1, "Test", "Testas");  // Pvz., administratoriaus ID ir vardas/pavardė
                        std::string message = admin.deleteSubjectFromDatabase(db, subject_id);

                        crow::response res;
                        res.code = 200;
                        res.body = "<html><head><meta http-equiv='refresh' content='2; url=/subjects'></head><body>"
                            + message +
                            "<br></body></html>";

                        return res;
                            });


    // Pridėjimo maršrutas su vardu ir pavarde egzistavimo patikrinimu
                    CROW_ROUTE(app, "/add_subject").methods("POST"_method)
                        ([&db](const crow::request& req) {
                        std::string body = req.body;
                        std::map<std::string, std::string> form_data;
                        std::istringstream body_stream(body);
                        std::string key_value_pair;

                        // Išskiriame duomenis iš POST užklausos
                        while (std::getline(body_stream, key_value_pair, '&')) {
                            size_t delimiter_pos = key_value_pair.find('=');
                            if (delimiter_pos != std::string::npos) {
                                std::string key = key_value_pair.substr(0, delimiter_pos);
                                std::string value = key_value_pair.substr(delimiter_pos + 1);
                                form_data[key] = value;
                            }
                        }

                        auto subject_name_it = form_data.find("subject_name");

                        crow::response res;
                        if (subject_name_it != form_data.end()) {
                            std::string subject_name = subject_name_it->second;

                            // Naudojame addSubjectToDatabase funkciją, kad patikrintume ir įrašytume dalyką
                            Administrator admin(1, "Test", "Testas");  // Pvz., administratoriaus ID ir vardas/pavardė
                            std::string message = admin.addSubjectToDatabase(db, subject_name);

                            // Atsakymas su pranešimu
                            res.code = 200;
                            res.body = "<html><head><meta http-equiv='refresh' content='2; url=/subjects'></head><body>"
                                + message +
                                "<br></body></html>";
                        }
                        else {
                            res.code = 400; // Netinkama užklausa, nes trūksta dalyko pavadinimo
                            res.body = "<html><head><meta http-equiv='refresh' content='2; url=/subjects'></head><body>"
                                "Destomo dalyko pavadinimas negali buti tuscias! "
                                "<br></body></html>";
                        }
                        return res;
                            });
                    // Pačių dėstomų dalykų langas (maršrutas)
                    CROW_ROUTE(app, "/subjects")
                        ([&db]() {
                        sql::Connection* con = db.connect();
                        std::string htmlContent = "<div style='display: flex; flex-direction: row;'>";

                        // Studentų sąrašo dalis (kairėje)
                        htmlContent += "<div style='width: 50%; padding: 10px;'>";
                        htmlContent += "<button onclick=\"window.location.href='/administratorius';\" style='padding: 10px; font-size: 1.2em;'>Atgal</button>";
                        htmlContent += "<h1>Destomu Dalyku sarasas</h1>";

                        // Naudojame Administratorius klasę, kad gautume dalykų sąrašą
                        Administrator admin(1, "Test", "Testas");
                        std::vector<std::tuple<int, std::string>> subjects = admin.getSubjectsFromDatabase(con);

                        // Dinamiškai generuojame HTML turinį pagal gautus dalykus
                        for (const auto& subject : subjects) {
                            int subject_id = std::get<0>(subject);  // Gauname dalyko ID
                            const std::string& subject_name = std::get<1>(subject);  // Gauname dalyko pavadinimą

                            htmlContent += "<div class='subject' style='margin-bottom: 20px; padding: 10px; font-size: 0.9em; border-bottom: 1px solid #ddd;'>";
                            htmlContent += "<p><strong>Destomo dalyko ID:</strong> " + std::to_string(subject_id) + "</p>";
                            htmlContent += "<p><strong>Destomo dalyko pavadinimas:</strong> " + subject_name + "</p>";
                            htmlContent += "</div>";
                        }

                        htmlContent += "</div>";  // Uždaryti pirmą div'ą

                        // Formos dalis (dešinėje)
                        htmlContent += "<div style='width: 50%; padding: 10px;'>";
                        htmlContent += "<h1>Valdymo panele</h1>";

                        // Pašalinti dalyką formą
                        htmlContent += "<h2>Pasalinti destoma dalyka pagal ID</h2>";
                        htmlContent += "<form action='/delete_subject' method='POST'>";
                        htmlContent += "<label for='subject_id'>Dalyko ID:</label>";
                        htmlContent += "<input type='number' id='subject_id' name='subject_id' required>";
                        htmlContent += "<button type='submit'>Pasalinti</button>";
                        htmlContent += "</form>";

                        // Pridėti dalyką formą
                        htmlContent += "<h2>Prideti nauja destoma dalyka</h2>";
                        htmlContent += "<form action='/add_subject' method='POST'>";
                        htmlContent += "<label for='subject_name'>Dalyko Pavadinimas:</label>";
                        htmlContent += "<input type='text' id='subject_name' name='subject_name' required><br>";
                        htmlContent += "<button type='submit'>Prideti</button>";
                        htmlContent += "</form>";

                        htmlContent += "</div></div>";  // Uždaryti antrą div'ą

                        // Grąžiname HTML turinį
                        crow::response res;
                        res.set_header("Content-Type", "text/html");
                        res.body = htmlContent;
                        return res;
                            });
                    //Maršrutas skirtas grupių ištrinimui.
    CROW_ROUTE(app, "/delete_group").methods("POST"_method)
        ([&db](const crow::request& req) {
        std::string body = req.body;
        std::string group_id_str;
        std::size_t pos = body.find("group_id=");

        if (pos != std::string::npos) {
            group_id_str = body.substr(pos + 9);
        }

        int group_id = std::stoi(group_id_str);

        // Administrator objekto sukūrimas
        Administrator admin(1, "Test", "Testas");

        // Pašalinti studentą iš duomenų bazės
        std::string result = admin.removeGroupFromDatabase(db, group_id);

        crow::response res;
        if (result == "Grupe pasalinta sėkmingai!") {
            res.code = 200;
            res.body = "<html><head><meta http-equiv='refresh' content='2; url=/groups'></head><body>"
                "Grupe su ID " + std::to_string(group_id) + " pašalinta sėkmingai. "
                "<br></body></html>";
        }
        else {
            res.code = 200;
            res.body = "<html><head><meta http-equiv='refresh' content='2; url=/groups'></head><body>"
                + result +
                "<br></body></html>";
        }

        return res;

            });

    // Pridėjimo maršrutas su vardu ir pavarde egzistavimo patikrinimu
            CROW_ROUTE(app, "/add_group").methods("POST"_method)
                ([&db](const crow::request& req) {
                std::string body = req.body;
                std::map<std::string, std::string> form_data;
                std::istringstream body_stream(body);
                std::string key_value_pair;

                while (std::getline(body_stream, key_value_pair, '&')) {
                    size_t delimiter_pos = key_value_pair.find('=');
                    if (delimiter_pos != std::string::npos) {
                        std::string key = key_value_pair.substr(0, delimiter_pos);
                        std::string value = key_value_pair.substr(delimiter_pos + 1);
                        form_data[key] = value;
                    }
                }

                auto group_name_it = form_data.find("group_name");

                crow::response res;
                if (group_name_it != form_data.end()) {
                    std::string group_name = group_name_it->second;

                    // Prisijungiam prie duomenų bazės
                    sql::Connection* con = db.connect();
                    Administrator admin(1, "Test", "Testas");

                    // Naudojame Administratorius metodą, kad gautume atsakymą
                    std::string response_body = admin.addGroupToDatabase(db, group_name);

                    // Uždarykite ryšį su duomenų baze
                    delete con;

                    res.code = 200;
                    res.body = response_body;
                }
                else {
                    res.code = 200;
                    res.body = "<html><head><meta http-equiv='refresh' content='2; url=/groups'></head><body>"
                        "Grupes pavadinimas negali buti tuscias! "
                        "<br></body></html>";
                }

                return res;
                    });
            // grupių langas
            CROW_ROUTE(app, "/groups")
                ([&db]() {
                // Prisijungiame prie duomenų bazės
                sql::Connection* con = db.connect();
                std::string htmlContent = "<div style='display: flex; flex-direction: row;'>";

                // Studentų sąrašo dalis (kairėje)
                htmlContent += "<div style='width: 50%; padding: 10px;'>";
                htmlContent += "<button onclick=\"window.location.href='/administratorius';\" style='padding: 10px; font-size: 1.2em;'>Atgal</button>";
                htmlContent += "<h1>Grupiu sarasas</h1>";

                // Paimame grupių informaciją iš duomenų bazės su funkcija getAllGroupsFromDatabase
                Administrator admin(1, "Test", "Testas"); // Sukuriame administratorių, čia tik pavyzdys
                std::vector<std::tuple<int, std::string>> groups = admin.getAllGroupsFromDatabase(con);

                // Patikriname, ar yra grupių ir generuojame HTML
                if (!groups.empty()) {
                    for (const auto& group : groups) {
                        int group_id = std::get<0>(group);  // Grupės ID
                        std::string group_name = std::get<1>(group);  // Grupės pavadinimas

                        htmlContent += "<div class='group' style='margin-bottom: 20px; padding: 10px; font-size: 0.9em; border-bottom: 1px solid #ddd;'>";
                        htmlContent += "<p><strong>Grupes ID:</strong> " + std::to_string(group_id) + "</p>";
                        htmlContent += "<p><strong>Grupes pavadinimas:</strong> " + group_name + "</p>";
                        htmlContent += "</div>";
                    }
                }
                else {
                    htmlContent += "<p>Nera grupiu.</p>";
                }

                htmlContent += "</div>";  // Užbaigia grupių sąrašo div'ą

                // Formos dalis (dešinėje)
                htmlContent += "<div style='width: 50%; padding: 10px;'>";
                htmlContent += "<h1>Valdymo panele</h1>";

                // Pašalinti grupę formą
                htmlContent += "<h2>Pasalinti grupe pagal ID</h2>";
                htmlContent += "<form action='/delete_group' method='POST'>";
                htmlContent += "<label for='group_id'>Grupes ID:</label>";
                htmlContent += "<input type='number' id='group_id' name='group_id' required>";
                htmlContent += "<button type='submit'>Pasalinti</button>";
                htmlContent += "</form>";

                // Pridėti grupę formą
                htmlContent += "<h2>Prideti nauja grupe</h2>";
                htmlContent += "<form action='/add_group' method='POST'>";
                htmlContent += "<label for='group_name'>Grupes Pavadinimas:</label>";
                htmlContent += "<input type='text' id='group_name' name='group_name' required><br>";
                htmlContent += "<button type='submit'>Prideti</button>";
                htmlContent += "</form>";

                htmlContent += "</div></div>";  // Uždaro pagrindinį div'ą

                // Grąžiname HTML turinį kaip atsakymą
                crow::response res;
                res.set_header("Content-Type", "text/html");
                res.body = htmlContent;
                return res;
                    });
            // Studento ištrinimo maršrutas (langas)
    CROW_ROUTE(app, "/delete_student").methods("POST"_method)
        ([&db](const crow::request& req) {
        std::string body = req.body;
        std::string student_id_str;
        std::size_t pos = body.find("student_id=");

        if (pos != std::string::npos) {
            student_id_str = body.substr(pos + 11);
        }

        int student_id = std::stoi(student_id_str);

        // Administrator objekto sukūrimas
        Administrator admin(1, "Test", "Testas");

        // Pašalinti studentą iš duomenų bazės
        std::string result = admin.deleteStudentFromDatabase(db, student_id);

        crow::response res;
        if (result == "Studentas pasalintas sekmingai!") {
            res.code = 200;
            res.body = "<html><head><meta http-equiv='refresh' content='2; url=/students'></head><body>"
                "Studentas su ID " + std::to_string(student_id) + " pasalintas sekmingai. "
                "<br></body></html>";
        }
        else {
            res.code = 200;
            res.body = "<html><head><meta http-equiv='refresh' content='2; url=/students'></head><body>"
                + result +
                "<br></body></html>";
        }

        return res;

            });


            // Pridėjimo maršrutas su vardu ir pavarde egzistavimo patikrinimu
            CROW_ROUTE(app, "/add_student").methods("POST"_method)
                ([&db](const crow::request& req) {
                std::string body = req.body;
                std::map<std::string, std::string> form_data;
                std::istringstream body_stream(body);
                std::string key_value_pair;

                while (std::getline(body_stream, key_value_pair, '&')) {
                    size_t delimiter_pos = key_value_pair.find('=');
                    if (delimiter_pos != std::string::npos) {
                        std::string key = key_value_pair.substr(0, delimiter_pos);
                        std::string value = key_value_pair.substr(delimiter_pos + 1);
                        form_data[key] = value;
                    }
                }

                auto name_it = form_data.find("name");
                auto surname_it = form_data.find("surname");

                crow::response res;
                if (name_it != form_data.end() && surname_it != form_data.end()) {
                    std::string name = name_it->second;
                    std::string surname = surname_it->second;

                    // Administrator objekto sukūrimas
                    Administrator admin(1, "Test", "Testas");

                    // Pridėti studentą ir gauti atsakymą
                    std::string result = admin.addStudentToDatabase(db, name, surname);

                    if (result == "Studentas pridetas sekmingai!") {
                        // Grąžinti sėkmės pranešimą
                        res.code = 200;
                        res.body = "<html><head><meta http-equiv='refresh' content='2; url=/students'></head><body>"
                            "Studentas pridetas sekmingai! "
                            "<br></body></html>";
                    }
                    else {
                        // Grąžinti klaidos pranešimą
                        res.code = 200;
                        res.body = "<html><head><meta http-equiv='refresh' content='2; url=/students'></head><body>"
                            + result +
                            "<br></body></html>";
                    }
                }
                else {
                    res.code = 200;
                    res.body = "<html><head><meta http-equiv='refresh' content='2; url=/students'></head><body>"
                        "Vardas ir pavarde negali buti tusti! "
                        "<br></body></html>";
                }
                return res;
                    });
            // Studento lango maršrutas.
            CROW_ROUTE(app, "/students")
                ([&db]() {
                sql::Connection* con = db.connect();
                Administrator admin(1, "Test", "Testas"); // Sukuriame administratorių, čia tik pavyzdys
                std::vector<std::tuple<int, std::string, std::string>> students = admin.getAllStudents(con);

                std::string htmlContent = "<div style='display: flex; flex-direction: row;'>";

                // Studentų sąrašo dalis (kairėje)
                htmlContent += "<div style='width: 50%; padding: 10px;'>";
                htmlContent += "<button onclick=\"window.location.href='/administratorius';\" style='padding: 10px; font-size: 1.2em;'>Atgal</button>";
                htmlContent += "<h1>Studentu sarasas</h1>";

                for (const auto& student : students) {
                    int student_id = std::get<0>(student);
                    std::string name = std::get<1>(student);
                    std::string surname = std::get<2>(student);

                    htmlContent += "<div class='student' style='margin-bottom: 20px; padding: 10px; font-size: 0.9em; border-bottom: 1px solid #ddd;'>";
                    htmlContent += "<p><strong>Student ID:</strong> " + std::to_string(student_id) + "</p>";
                    htmlContent += "<p><strong>Vardas:</strong> " + name + "</p>";
                    htmlContent += "<p><strong>Pavarde:</strong> " + surname + "</p>";
                    htmlContent += "</div>";
                }

                htmlContent += "</div>";

                // Formos dalis (dešinėje)
                htmlContent += "<div style='width: 50%; padding: 10px;'>";
                htmlContent += "<h1>Valdymo panele</h1>";

                // Pašalinti studentą formą
                htmlContent += "<h2>Pasalinti studenta pagal ID</h2>";
                htmlContent += "<form action='/delete_student' method='POST'>";
                htmlContent += "<label for='student_id'>Student ID:</label>";
                htmlContent += "<input type='number' id='student_id' name='student_id' required>";
                htmlContent += "<button type='submit'>Pasalinti</button>";
                htmlContent += "</form>";

                // Pridėti studentą formą
                htmlContent += "<h2>Prideti nauja studenta</h2>";
                htmlContent += "<form action='/add_student' method='POST'>";
                htmlContent += "<label for='name'>Vardas:</label>";
                htmlContent += "<input type='text' id='name' name='name' required><br>";
                htmlContent += "<label for='surname'>Pavarde:</label>";
                htmlContent += "<input type='text' id='surname' name='surname' required><br>";
                htmlContent += "<button type='submit'>Prideti</button>";
                htmlContent += "</form>";

                htmlContent += "</div></div>";  // Uždaryti pagrindinį div'ą

                // Grąžiname HTML turinį
                crow::response res;
                res.set_header("Content-Type", "text/html");
                res.body = htmlContent;
                return res;
                    });
            // Maršrutas dėstytojo ištrinimui.
            CROW_ROUTE(app, "/delete_teacher").methods("POST"_method)
                ([&db](const crow::request& req) {
                std::string body = req.body;
                std::string teacher_id_str;
                std::size_t pos = body.find("teacher_id=");

                if (pos != std::string::npos) {
                    teacher_id_str = body.substr(pos + 11);
                }

                int teacher_id = std::stoi(teacher_id_str);

                // Administrator objekto sukūrimas
                Administrator admin(1, "Test", "Testas");

                // Pašalinti studentą iš duomenų bazės
                std::string result = admin.removeTeacherFromDatabase(db, teacher_id);

                crow::response res;
                if (result == "Destytojas pasalintas sekmingai!") {
                    res.code = 200;
                    res.body = "<html><head><meta http-equiv='refresh' content='2; url=/teachers'></head><body>"
                        "Destytojas su ID " + std::to_string(teacher_id) + " pasalintas sekmingai. "
                        "<br></body></html>";
                }
                else {
                    res.code = 200;
                    res.body = "<html><head><meta http-equiv='refresh' content='2; url=/teachers'></head><body>"
                        + result +
                        "<br></body></html>";
                }
                return res;
                    });

            // Maršrutas pridėti dėstytoją
            CROW_ROUTE(app, "/add_teacher").methods("POST"_method)
                ([&db](const crow::request& req) {
                std::string body = req.body;
                std::map<std::string, std::string> form_data;
                std::istringstream body_stream(body);
                std::string key_value_pair;

                while (std::getline(body_stream, key_value_pair, '&')) {
                    size_t delimiter_pos = key_value_pair.find('=');
                    if (delimiter_pos != std::string::npos) {
                        std::string key = key_value_pair.substr(0, delimiter_pos);
                        std::string value = key_value_pair.substr(delimiter_pos + 1);
                        form_data[key] = value;
                    }
                }

                auto name_it = form_data.find("name");
                auto surname_it = form_data.find("surname");

                crow::response res;
                if (name_it != form_data.end() && surname_it != form_data.end()) {
                    std::string name = name_it->second;
                    std::string surname = surname_it->second;

                    Administrator admin(1, "Admin", "Test");
                    std::string result = admin.addTeacherToDatabase(db, name, surname);

                    res.code = 200;
                    res.body = "<html><head><meta http-equiv='refresh' content='2; url=/teachers'></head><body>"
                        + result +
                        "<br></body></html>";
                }
                else {
                    res.code = 400;
                    res.body = "<html><head><meta http-equiv='refresh' content='2; url=/teachers'></head><body>"
                        "Vardas ir pavarde negali buti tusti! "
                        "<br></body></html>";
                }

                return res;
                    });
            // pgr. dėstytojų langas (maršrutas)
            CROW_ROUTE(app, "/teachers")
                ([&db]() {
                sql::Connection* con = db.connect();
                std::string htmlContent = "<div style='display: flex; flex-direction: row;'>";

                // Studentų sąrašo dalis (kairėje)
                htmlContent += "<div style='width: 50%; padding: 10px;'>";
                htmlContent += "<button onclick=\"window.location.href='/administratorius';\" style='padding: 10px; font-size: 1.2em;'>Atgal</button>";
                htmlContent += "<h1>Destytoju sarasas</h1>";

                if (con) {
                    Administrator admin(1, "Test", "Testas");
                    auto teachers = admin.getAllTeachers(con);

                    for (const auto& teacher : teachers) {
                        int teacher_id;
                        std::string name, surname;
                        std::tie(teacher_id, name, surname) = teacher;

                        htmlContent += "<div class='teacher' style='margin-bottom: 20px; padding: 10px; font-size: 0.9em; border-bottom: 1px solid #ddd;'>";
                        htmlContent += "<p><strong>Teacher ID:</strong> " + std::to_string(teacher_id) + "</p>";
                        htmlContent += "<p><strong>Vardas:</strong> " + name + "</p>";
                        htmlContent += "<p><strong>Pavarde:</strong> " + surname + "</p>";
                        htmlContent += "</div>";
                    }

                    delete con; // Išjungiam ryšį su duomenų baze
                }
                else {
                    htmlContent += "<p>Klaida: Nepavyko prisijungti prie duomenu bazes.</p>";
                }

                htmlContent += "</div>";

                // Formos dalis (dešinėje)
                htmlContent += "<div style='width: 50%; padding: 10px;'>";
                htmlContent += "<h1>Valdymo panele</h1>";

                // Pašalinti dėstytoją formą
                htmlContent += "<h2>Pasalinti destytoja pagal ID</h2>";
                htmlContent += "<form action='/delete_teacher' method='POST'>";
                htmlContent += "<label for='teacher_id'>Teacher ID:</label>";
                htmlContent += "<input type='number' id='teacher_id' name='teacher_id' required>";
                htmlContent += "<button type='submit'>Pasalinti</button>";
                htmlContent += "</form>";

                // Pridėti dėstytoją formą
                htmlContent += "<h2>Prideti nauja destytoja</h2>";
                htmlContent += "<form action='/add_teacher' method='POST'>";
                htmlContent += "<label for='name'>Vardas:</label>";
                htmlContent += "<input type='text' id='name' name='name' required><br>";
                htmlContent += "<label for='surname'>Pavarde:</label>";
                htmlContent += "<input type='text' id='surname' name='surname' required><br>";
                htmlContent += "<button type='submit'>Prideti</button>";
                htmlContent += "</form>";

                htmlContent += "</div></div>";  // Uždaryti pagrindinį div'ą

                // Grąžiname HTML turinį
                crow::response res;
                res.set_header("Content-Type", "text/html");
                res.body = htmlContent;
                return res;
                    });

    // Paleisti serverį
    app.port(8080).multithreaded().run();
    
}