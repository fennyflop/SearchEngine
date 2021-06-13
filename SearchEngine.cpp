#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

#define ASSERT(expr) (!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))

#define RUN_TEST(test) RunTest((test), #test) 

const int MAX_RESULT_DOCUMENT_COUNT = 5;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            words.push_back(word);
            word = "";
        } else {
            word += c;
        }
    }
    words.push_back(word);
    
    return words;
}
    
struct Document {
    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    
    inline static constexpr int INVALID_DOCUMENT_ID = -1;

    SearchServer() = default;
    
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words) {
        set<string> unique_words;
        for (const string& word : stop_words) {
            if (!IsValidWord(word)) {
                throw invalid_argument("Стоп слова не должны содержать недопустимые символы"); 
            } else {
                unique_words.insert(word);
            }
        }
        stop_words_ = unique_words;
    }

    explicit SearchServer(const string& stop_words_text)  {
        if (!CheckQueryValidity(stop_words_text)) {
           throw invalid_argument("Стоп слова не должны содержать недопустимые символы"); 
        }
        stop_words_ = MakeUniqueSet(SplitIntoWords(stop_words_text));
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
        // Проверка на отрицательный id и на существующий id
        if (document_id < 0 || documents_.count(document_id)) {
            throw invalid_argument("Нельзя добавлять документы с отрицательным id или уже существуюищим id");
        }
        
        if (!CheckQueryValidity(document)) {
            throw invalid_argument("Нельзя использовать недопустимые символы в документах");
        }
        
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
        document_list_.push_back(document_id); // Храним порядок id
    }

    template <typename DocumentPredicate>
     vector<Document> FindTopDocuments(const string& raw_query, DocumentPredicate document_predicate) const {
        
        if (!CheckQueryValidity(raw_query)) {
            throw invalid_argument("Поиск не должен содержать недопустимых символов, болтающихся маркеров или двойных '-'.");
        }
        
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, document_predicate);

        sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
            if (abs(lhs.relevance - rhs.relevance) < 1e-6) {
                return lhs.rating > rhs.rating;
            } else {
                return lhs.relevance > rhs.relevance;
            }
        });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
        });
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const {
        return documents_.size();
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        
        if (!CheckQueryValidity(raw_query)) {
            throw invalid_argument("Поиск не должен содержать недопустимых символов, болтающихся маркеров или двойных '-'.");
        }
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        // result = {matched_words, documents_.at(document_id).status};
        return {matched_words, documents_.at(document_id).status};
    }
    
    int GetDocumentId(int index) const {
        return document_list_.at(index);
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };
    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;
    vector<int> document_list_;
    
    static bool IsValidWord(const string& word) {
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
        });
    }

    static bool CheckQueryValidity (const string& raw_query){
        bool answer = true;
        for (const string& word : SplitIntoWords(raw_query)) {
            // Проверка на одинокий минус
            if (word == "-") {
                answer = false;
                break;
            }
            // Проверка на двойной минус
            if (word[0] == '-' && word[1] == '-') {
                answer = false;
                break;
            }
            // Проверка на спец. символы
            if (!IsValidWord(word)) {
                // cout << word << ' ' << IsValidWord(word) << endl;
                answer = false;
                break;
            }
        }
        return answer;
    }
    
    template <typename StringContainer>
    set<string> MakeUniqueSet(const StringContainer& container) {
        set<string> unique_set;
        for (const string& word : container) {
            unique_set.insert(word);
        }
        return unique_set;
    }

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return {text, is_minus, IsStopWord(text)};
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                } else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template <typename DocumentPredicate>
    vector<Document> FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status, document_data.rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
        }
        return matched_documents;
    }
};

void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                const string& hint) {
    if (!value) {
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cout << boolalpha;
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cout << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

template <typename TestType>
void RunTest(const TestType& test, const string& test_name) {
    test();
    cerr << test_name << " OK" << endl;
}

void TestDocumentAddition () {
    const string content = "content"s;
    const vector<int> ratings = {0};
    {
    SearchServer server;
    server.AddDocument(0, content, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(1, content, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(2, content, DocumentStatus::ACTUAL, ratings);
    ASSERT_EQUAL_HINT(server.FindTopDocuments("content"s).size(), 3, "Must have 3 documnets");
    ASSERT_HINT(server.FindTopDocuments("cat"s).empty(), "Must have no results");
    }
}


// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов
void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    // Сначала убеждаемся, что поиск слова, не входящего в список стоп-слов,
    // находит нужный документ
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL_HINT(found_docs.size(), 1, "Must have no results");
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL_HINT(doc0.id, doc_id, "Document ID must be 42");
    }

    // Затем убеждаемся, что поиск этого же слова, входящего в список стоп-слов,
    // возвращает пустой результат
    {
        SearchServer server("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(), "Must be empty");
    }
}

void TestMinusWordAddition () {
    SearchServer server;
    const vector<int> ratings = {0};
    server.AddDocument(0, "cat with fur"s, DocumentStatus::ACTUAL, ratings);
    ASSERT_EQUAL_HINT(server.FindTopDocuments("cat").size(), 1, "Must have 1 result");
    ASSERT_HINT(server.FindTopDocuments("cat -fur").empty(), "Must be empty");
}

void TestDocumentMatching() {
    SearchServer server;
    const vector<int> ratings = {0};
    server.AddDocument(0, "cat with fur"s, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(1, "cat with hat"s, DocumentStatus::ACTUAL, ratings);
    tuple<vector<string>, DocumentStatus> answer = server.MatchDocument("cat fur"s, 0);
    ASSERT_HINT((get<0>(answer)[0] == "cat"s && get<0>(answer)[1] == "fur"s && get<0>(answer).size() == 2), "Must have 2 elements in the vector");
    tuple<vector<string>, DocumentStatus> answer_empty = server.MatchDocument("cat -hat"s, 1);
    ASSERT_HINT(get<0>(answer_empty).empty(), "Must be empty");
}

void TestRelevanceSorting() {
    SearchServer server("и в на"s);
    const vector<int> ratings = {0};
    server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, ratings);
    const auto found_docs = server.FindTopDocuments("пушистый ухоженный кот"s);
    ASSERT_HINT((found_docs[0].relevance > found_docs[1].relevance && found_docs[1].relevance > found_docs[2].relevance), "Relevance sorting must go highest to lowest");
}

void TestRatingCounting () {
    SearchServer server;
    const vector<int> ratings = {2, 61, 42};
    server.AddDocument(0, "белый кот"s, DocumentStatus::ACTUAL, ratings);
    ASSERT_EQUAL_HINT(server.FindTopDocuments("белый кот"s)[0].rating, 35, "Document ID must be 35");
}

void TestRelevanceCounting () {
    SearchServer server("и в на"s);
    const vector<int> ratings = {0};
    server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, ratings);
    const auto found_docs = server.FindTopDocuments("пушистый ухоженный кот"s);
    ASSERT_HINT((round(found_docs[0].relevance * 1000000) == 650672 && round(found_docs[1].relevance * 1000000) == 274653 && round(found_docs[2].relevance * 1000000) == 101366), "Relevance must be calculated correctly");
}

void TestKeyMapperSort() {
    SearchServer server;
    const vector<int> ratings = {0};
    server.AddDocument(0, "dog"s, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(1, "dog"s, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(2, "dog"s, DocumentStatus::REMOVED, ratings);
    server.AddDocument(3, "dog"s, DocumentStatus::BANNED, ratings);
    {
    const auto found_docs = server.FindTopDocuments("dog"s, [] (
    [[maybe_unused]]const int& doc_id, [[maybe_unused]]const DocumentStatus& doc_status, [[maybe_unused]]const int& rating
    ) {
        return doc_id % 2 == 0 && doc_status == DocumentStatus::REMOVED;
    });
    ASSERT_EQUAL_HINT(found_docs[0].id, 2, "Document ID must be 2");
    }
    {
    const auto found_docs = server.FindTopDocuments("dog"s, [] (
    [[maybe_unused]]const int& doc_id, [[maybe_unused]]const DocumentStatus& doc_status, [[maybe_unused]]const int& rating
    ) {
        return doc_id % 2 == 0 && doc_status != DocumentStatus::REMOVED;
    });
    ASSERT_EQUAL_HINT(found_docs[0].id, 0, "Document ID must be 0");
    }
        {
    const auto found_docs = server.FindTopDocuments("dog"s, [] (
    [[maybe_unused]]const int& doc_id, [[maybe_unused]]const DocumentStatus& doc_status, [[maybe_unused]]const int& rating
    ) {
        return doc_id % 2 == 0 && doc_status == DocumentStatus::BANNED;
    });
    ASSERT_HINT(found_docs.empty(), "Must be empty");
    }
}

void TestDocumentStatus () {
    SearchServer server;
    const vector<int> ratings = {0};
    server.AddDocument(0, "dog"s, DocumentStatus::ACTUAL, ratings);
    server.AddDocument(1, "dog"s, DocumentStatus::IRRELEVANT, ratings);
    server.AddDocument(2, "dog"s, DocumentStatus::REMOVED, ratings);
    server.AddDocument(3, "dog"s, DocumentStatus::BANNED, ratings);
    ASSERT_EQUAL_HINT(server.FindTopDocuments("dog"s, DocumentStatus::ACTUAL)[0].id, 0, "Document ID must be 0");
    ASSERT_EQUAL_HINT(server.FindTopDocuments("dog"s, DocumentStatus::IRRELEVANT)[0].id,1, "Document ID must be 1");
    ASSERT_EQUAL_HINT(server.FindTopDocuments("dog"s, DocumentStatus::REMOVED)[0].id, 2, "Document ID must be 2");
    ASSERT_EQUAL_HINT(server.FindTopDocuments("dog"s, DocumentStatus::BANNED)[0].id, 3, "Document ID must be 3");
}

/*
Разместите код остальных тестов здесь
*/

// Функция TestSearchServer является точкой входа для запуска тестов
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestDocumentAddition);
    RUN_TEST(TestMinusWordAddition);
    RUN_TEST(TestDocumentMatching);
    RUN_TEST(TestRelevanceSorting);
    RUN_TEST(TestRatingCounting);
    RUN_TEST(TestRelevanceCounting);
    RUN_TEST(TestKeyMapperSort);
    RUN_TEST(TestDocumentStatus);
    // Не забудьте вызывать остальные тесты здесь
}

// ==================== для примера =========================
void PrintDocument(const Document& document) {
    cout << "{ "s
         << "document_id = "s << document.id << ", "s
         << "relevance = "s << document.relevance << ", "s
         << "rating = "s << document.rating << " }"s << endl;
}

void PrintMatchDocumentResult(int document_id, const vector<string>& words, DocumentStatus status) {
    cout << "{ "s
         << "document_id = "s << document_id << ", "s
         << "status = "s << static_cast<int>(status) << ", "s
         << "words ="s;
    for (const string& word : words) {
        cout << ' ' << word;
    }
    cout << "}"s << endl;
}

void AddDocument(SearchServer& search_server, int document_id, const string& document, DocumentStatus status,
                 const vector<int>& ratings) {
    try {
        search_server.AddDocument(document_id, document, status, ratings);
    } catch (const exception& e) {
        cout << "Ошибка добавления документа "s << document_id << ": "s << e.what() << endl;
    }
}

void FindTopDocuments(const SearchServer& search_server, const string& raw_query) {
    cout << "Результаты поиска по запросу: "s << raw_query << endl;
    try {
        for (const Document& document : search_server.FindTopDocuments(raw_query)) {
            PrintDocument(document);
        }
    } catch (const exception& e) {
        cout << "Ошибка поиска: "s << e.what() << endl;
    }
}

void MatchDocuments(const SearchServer& search_server, const string& query) {
    try {
        cout << "Матчинг документов по запросу: "s << query << endl;
        const int document_count = search_server.GetDocumentCount();
        for (int index = 0; index < document_count; ++index) {
            const int document_id = search_server.GetDocumentId(index);
            const auto [words, status] = search_server.MatchDocument(query, document_id);
            PrintMatchDocumentResult(document_id, words, status);
        }
    } catch (const exception& e) {
        cout << "Ошибка матчинга документов на запрос "s << query << ": "s << e.what() << endl;
    }
}

int main() {
    
    TestSearchServer();

    SearchServer search_server("и в на"s);

    AddDocument(search_server, 5, "пушистый кот пушистый хвост и"s, DocumentStatus::ACTUAL, {7, 2, 7});
    AddDocument(search_server, 1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, {1, 2});
    AddDocument(search_server, -1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, {1, 2});
    AddDocument(search_server, 3, "большой пёс скво\x12рец евгений"s, DocumentStatus::ACTUAL, {1, 3, 2});
    AddDocument(search_server, 4, "большой пёс скворец евгений"s, DocumentStatus::ACTUAL, {1, 1, 1});

    FindTopDocuments(search_server, "и в на"s);
    FindTopDocuments(search_server, "пушистый -пёс"s);
    FindTopDocuments(search_server, "пушистый --кот"s);
    FindTopDocuments(search_server, "пушистый -"s);
    
    MatchDocuments(search_server, "пушистый пёс"s);
    MatchDocuments(search_server, "модный -кот"s);
    MatchDocuments(search_server, "модный --пёс"s);
    MatchDocuments(search_server, "пушистый - хвост"s);
    
    try {
        search_server.GetDocumentId(1);
    } catch (const out_of_range& error){
        cout << error.what() << endl;
    }
    
} 