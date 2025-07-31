#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <iomanip>
#include <list>

/**
 * @brief A value for prices that are undefined or not applicable.
 */
const int64_t UNDEFINED_PRICE = -9223372036854775807LL;

/** 
* @brief Namespace for action types in the market data. 
*/
namespace Act {
enum Type : char { Add = 'A', Cancel = 'C', Modify = 'M', Clear = 'R', Trade = 'T', Fill = 'F', None = 'N' };
inline std::string ToStr(Type a) {
    switch (a) {
        case Add: return "Add"; case Cancel: return "Cancel"; case Modify: return "Modify"; case Clear: return "Clear";
        case Trade: return "Trade"; case Fill: return "Fill"; case None: return "None";
    }
    return "Unknown";
}}

/**
 * @brief Namespace for side types in the market data.
 */
namespace Sd {
enum Type : char { Ask = 'A', Bid = 'B', None = 'N' };
inline std::string ToStr(Type s) {
    switch (s) {
        case Ask: return "Ask"; case Bid: return "Bid"; case None: return "None";
    }
    return "Unknown";
}}

/**
 * @brief Structure representing a single Market By Order (MBO) message.
 */
struct MboSingle {
    std::string tsRecv;
    std::string tsEvent;
    uint8_t rtype;
    uint16_t pubId;
    uint32_t instrId;
    Act::Type action;
    Sd::Type side;
    int64_t price;
    uint32_t size;
    uint8_t chanId;
    uint64_t orderId;
    uint8_t flags;
    int32_t tsInDelta;
    uint32_t sequence;
    std::string symbol;
};

/**
 * @brief Scale factor for converting prices to/from nanoseconds.
 */
const double PRICE_SCALE = 1e9;


/**
 * @brief Converts a price in nanoseconds to a double representation.
 */
inline double ToDblPrice(int64_t priceNano) {
    if (priceNano == UNDEFINED_PRICE) return NAN;
    return static_cast<double>(priceNano) / PRICE_SCALE;
}

/**
 * @brief Converts a double price to its nanosecond representation.
 */
inline int64_t ToNanoPrice(double priceDbl) {
    if (std::isnan(priceDbl)) return UNDEFINED_PRICE;
    return static_cast<int64_t>(std::round(priceDbl * PRICE_SCALE));
}


/**
 * @brief Structure representing a price level in the order book.
 * 
 * combined information for all orders at a specific price point,
 * including the total quantity available and the number of individual orders.
 */
struct PriceLvl {
    int64_t price {UNDEFINED_PRICE};
    uint32_t size {0};
    uint32_t count {0};
    bool IsEmpty() const { return price == UNDEFINED_PRICE; }
    operator bool() const { return !IsEmpty(); }
};


/**
 * @brief For easy printing of priceLvl object to an outputstream.
 * @param stream The output stream (e.g., std::cout, std::ofstream).
 * @param level The PriceLvl object to print.
 * @return A reference to the output stream, enabling chaining of operations.
 */
std::ostream& operator<<(std::ostream& stream, const PriceLvl& level) {
    stream << level.size << " @ " << std::fixed << std::setprecision(9) << ToDblPrice(level.price) << " | "
           << level.count << " order(s)";
    return stream;
}


/**
 * @brief For easy printing of MboSingle object to an outputstream.
 * @param os The output stream (e.g., std::cout, std::ofstream).
 * @param m The MboSingle object to print.
 * @return A reference to the output stream, enabling chaining of operations.
 */
std::ostream& operator<<(std::ostream& os, const MboSingle& m) {
    os << "MboSingle { tsRecv: " << m.tsRecv
       << ", tsEvent: " << m.tsEvent
       << ", rtype: " << static_cast<int>(m.rtype)
       << ", pubId: " << m.pubId
       << ", instrId: " << m.instrId
       << ", action: '" << Act::ToStr(m.action) << "'"
       << ", side: '" << Sd::ToStr(m.side) << "'"
       << ", price: " << std::fixed << std::setprecision(9) << ToDblPrice(m.price)
       << ", size: " << m.size
       << ", chanId: " << static_cast<int>(m.chanId)
       << ", orderId: " << m.orderId
       << ", flags: " << static_cast<int>(m.flags)
       << ", tsInDelta: " << m.tsInDelta
       << ", sequence: " << m.sequence
       << ", symbol: " << m.symbol << " }";
    return os;
}


/**
 * @brief Class representing a market order book.Which is being deferentiated on instrumentId and publisherId which is managed by market class.
 * 
 */
class Book {
public:

    /**
     * @brief Returns the best bid and ask price levels.
     */
    std::pair<PriceLvl, PriceLvl> Bbo() const { 
        return {GetBidLvl(0), GetAskLvl(0)}; 
    }

    /**
     * @brief Returns the bid price level at a given index.
     * @param idx The index of the bid level to retrieve.
     */
    PriceLvl GetBidLvl(size_t idx) const {
        if (bids_.size() > idx) { auto it = bids_.rbegin(); std::advance(it, idx); return CalcPriceLvl(it->first, it->second); }
        return PriceLvl{};
    }

    /**
     * @brief Returns the ask price level at a given index.
     * @param idx The index of the ask level to retrieve.
     */
    PriceLvl GetAskLvl(size_t idx) const {
        if (offers_.size() > idx) { auto it = offers_.begin(); std::advance(it, idx); return CalcPriceLvl(it->first, it->second); }
        return PriceLvl{};
    }

    /**
     * @brief Returns a vector of aggregated bid price levels.
     * @param numLvls The number of bid levels to retrieve.
     */
    std::vector<PriceLvl> GetBidLvls(size_t numLvls) const {
        std::vector<PriceLvl> lvls; lvls.reserve(numLvls); auto it = bids_.rbegin();
        for (size_t i = 0; i < numLvls && it != bids_.rend(); ++i, ++it) lvls.emplace_back(CalcPriceLvl(it->first, it->second));
        return lvls;
    }

    /**
     * @brief Returns a vector of aggregated ask price levels.
     * @param numLvls The number of ask levels to retrieve.
     */
    std::vector<PriceLvl> GetAskLvls(size_t numLvls) const {
        std::vector<PriceLvl> lvls; lvls.reserve(numLvls); auto it = offers_.begin();
        for (size_t i = 0; i < numLvls && it != offers_.end(); ++i, ++it) lvls.emplace_back(CalcPriceLvl(it->first, it->second));
        return lvls;
    }

    /**
     * @brief Calculates the depth of a bid level at a specific price.
     * @param price The price of the bid level.
     */
    uint32_t GetBidLevelDepth(int64_t price) const {
        uint32_t depth = 0;
        for (auto r_it = bids_.rbegin(); r_it != bids_.rend(); ++r_it) {
            if (r_it->first == price) {
                return depth;
            }
            if (r_it->first < price) {
                return 0;
            }
            ++depth;
        }
        return 0;
    }

    /**
     * @brief Calculates the depth of an ask level at a specific price.
     * @param price The price of the ask level.
     */
    uint32_t GetAskLevelDepth(int64_t price) const {
        auto it = offers_.lower_bound(price);
        if (it == offers_.end() || it->first != price) {
            return 0;
        }
        return std::distance(offers_.begin(), it);
    }

    /**
     * @brief Applies a market by order message to the book.
     */
    void Apply(const MboSingle& m) {
        switch (m.action) {
            case Act::Clear: Clear(); break;
            case Act::Add: Add(m); break;
            case Act::Cancel: Cancel(m); break;
            case Act::Modify: Modify(m); break;
            case Act::Trade: case Act::Fill: case Act::None: break;
            default: std::cerr << "Unknown action: " << Act::ToStr(m.action) << ". Ignoring.\n";
        }
    }

    /**
     * @brief Processes a synthetic trade by adjusting the book.
     * @param px The price of the synthetic trade.
     * @param sz The size of the synthetic trade.
     * @param sideAff The side affected by the synthetic trade (Bid or Ask).
     */
    void ProcSynthTrade(int64_t px, uint32_t sz, Sd::Type sideAff) {
        LvlOrds& affLvls = GetSdOrds(sideAff);
        auto lvlIt = affLvls.find(px);
        if (lvlIt == affLvls.end()) {
            std::cerr << "Warn: Synth trade at non-existent lvl " << Sd::ToStr(sideAff) << " @ "
                      << std::fixed << std::setprecision(9) << ToDblPrice(px) << " sz " << sz << ". Ign.\n";
            return;
        }
        LvlOrdsInQ& ordsAtLvl = lvlIt->second;
        uint32_t remSz = sz;
        auto curOrd = ordsAtLvl.begin();
        while (curOrd != ordsAtLvl.end() && remSz > 0) {
            if (curOrd->size <= remSz) {
                remSz -= curOrd->size;
                ordsById_.erase(curOrd->orderId);
                curOrd = ordsAtLvl.erase(curOrd);
            } else {
                curOrd->size -= remSz;
                remSz = 0;
            }
        }
        if (ordsAtLvl.empty()) RemLvl(sideAff, px);
    }

private:
    /**
     * @brief Type for storing orders at a specific price level.
     */
    using LvlOrdsInQ = std::list<MboSingle>;
    /**
     * @brief Type for mapping order IDs to their price and side.
     */
    struct PxAndSd { int64_t price; Sd::Type side; };
    /**
     * @brief Type for mapping order IDs to their price and side.
     */
    using OrdsById = std::unordered_map<uint64_t, PxAndSd>;
    /**
     * @brief Type for storing orders by price level.
     */
    using LvlOrds = std::map<int64_t, LvlOrdsInQ>;


    /**
     * @brief Calculates the aggregated price level from a list of orders at a specific price.
     * @param px The price level.
     * @param lvl The list of orders at that price level.
     * @return A PriceLvl object containing the aggregated size and count of orders at that price.
     */
    static PriceLvl CalcPriceLvl(int64_t px, const LvlOrdsInQ& lvl) {
        PriceLvl res{px};
        for (const auto& ord : lvl) {
            ++res.count;
            res.size += ord.size;
        }
        return res;
    }

    /**
     * @brief Finds an order in a specific price level.
     * @param lvl The list of orders at that price level.
     * @param ordId The order ID to find.
     * @return An iterator to the order if found, otherwise an iterator to the end of the list.
     */
    LvlOrdsInQ::iterator GetLvlOrd(LvlOrdsInQ& lvl, uint64_t ordId) {
        return std::find_if(lvl.begin(), lvl.end(), [ordId](const MboSingle& o) { return o.orderId == ordId; });
    }

    /**
     * @brief Clears the book, removing all orders.
     */
    void Clear() { 
        ordsById_.clear(); offers_.clear(); bids_.clear(); 
    }

    /**
     * @brief Adds a new order to the book.
     * @param m The MboSingle message containing the order details.
     */
    void Add(MboSingle m) {
        LvlOrdsInQ& lvl = GetOrInsLvl(m.side, m.price);
        lvl.emplace_back(std::move(m));
        auto r = ordsById_.emplace(m.orderId, PxAndSd{m.price, m.side});
        if (!r.second) throw std::invalid_argument{"Dupe ID " + std::to_string(m.orderId) + " for Add"};
    }

    /**
     * @brief Cancels an order in the book.
     * @param m The MboSingle message containing the order ID and details.
     */
    void Cancel(MboSingle m) {
        auto psIt = ordsById_.find(m.orderId);
        if (psIt == ordsById_.end()) { std::cerr << "Warn: Cancel unk ID " + std::to_string(m.orderId) + ". Ign.\n"; return; }
        LvlOrdsInQ& lvl = GetLvl(psIt->second.side, psIt->second.price);
        auto ordIt = GetLvlOrd(lvl, m.orderId);
        if (ordIt == lvl.end()) { std::cerr << "IntErr: ID " + std::to_string(m.orderId) + " in map but not in list.\n"; return; }
        if (ordIt->size < m.size) { std::cerr << "Warn: Partial cancel > existing sz. ID " + std::to_string(m.orderId) + ". Cap to 0.\n"; ordIt->size = 0; }
        else ordIt->size -= m.size;
        if (ordIt->size == 0) {
            ordsById_.erase(m.orderId);
            lvl.erase(ordIt);
            if (lvl.empty()) RemLvl(psIt->second.side, psIt->second.price);
        }
    }

    /**
     * @brief Modifies an existing order in the book.
     * @param m The MboSingle message containing the updated order details.
     */
    void Modify(MboSingle m) {
        auto psIt = ordsById_.find(m.orderId);
        if (psIt == ordsById_.end()) { Add(m); return; }
        if (psIt->second.side != m.side) throw std::logic_error{"ID " + std::to_string(m.orderId) + " changed side."};
        int64_t prevPx = psIt->second.price;
        LvlOrdsInQ& prevLvl = GetLvl(m.side, prevPx);
        auto ordIt = GetLvlOrd(prevLvl, m.orderId);
        if (ordIt == prevLvl.end()) { std::cerr << "IntErr: ID " + std::to_string(m.orderId) + " in map but not in prev list.\n"; return; }
        if (prevPx != m.price) {
            psIt->second.price = m.price;
            prevLvl.erase(ordIt);
            if (prevLvl.empty()) RemLvl(m.side, prevPx);
            LvlOrdsInQ& newLvl = GetOrInsLvl(m.side, m.price);
            newLvl.emplace_back(std::move(m));
        } else {
            if (ordIt->size < m.size) {
                prevLvl.erase(ordIt);
                prevLvl.emplace_back(std::move(m));
            } else {
                ordIt->size = m.size;
            }
        }
    }
    /**
     * @brief Gets the orders for a specific side (Bid or Ask).
     * @param s The side type (Bid or Ask).
     * @return A reference to the map of orders for that side.
     */
    LvlOrds& GetSdOrds(Sd::Type s) {
        switch (s) { case Sd::Ask: return offers_; case Sd::Bid: return bids_; default: throw std::invalid_argument{"Invalid side."}; }
    }

    /**
     * @brief Gets the orders for a specific side and price level.
     * @param s The side type (Bid or Ask).
     * @param px The price level.
     * @return A reference to the list of orders at that price level.
     */
    LvlOrdsInQ& GetLvl(Sd::Type s, int64_t px) {
        LvlOrds& lvls = GetSdOrds(s); auto lvlIt = lvls.find(px);
        if (lvlIt == lvls.end()) {
             std::ostringstream oss;
             oss << "Access unk lvl " << Sd::ToStr(s) << " @ " << std::fixed << std::setprecision(9) << ToDblPrice(px);
             throw std::invalid_argument{oss.str()};
        }
        return lvlIt->second;
    }

    /**
     * @brief Gets or inserts a new level for a specific side and price.
     * @param s The side type (Bid or Ask).
     * @param px The price level.
     * @return A reference to the list of orders at that price level, creating it if it doesn't exist.
     */
    LvlOrdsInQ& GetOrInsLvl(Sd::Type s, int64_t px) { 
        return GetSdOrds(s)[px]; 
    }

    /**
     * @brief Removes a level for a specific side and price.
     * @param s The side type (Bid or Ask).
     * @param px The price level to remove.
     */
    void RemLvl(Sd::Type s, int64_t px) { GetSdOrds(s).erase(px); }

    /**
     * @brief Maps order IDs to their price and side.
     */
    OrdsById ordsById_;
    /**
     * @brief Maps ask price levels to their orders.
     */
    LvlOrds offers_;
    /**
     * @brief Maps bid price levels to their orders.
     */
    LvlOrds bids_;
};


/**
 * @brief Class representing a market, which contains multiple books differentiated by instrument ID and publisher ID.
 */
class Market {
public:

    /**
    * @brief Retrieves aggregated bid price levels for a specific instrument across all publishers.
    * @param instrId The unique identifier of the financial instrument.
    * @param numLvls The maximum number of aggregated price levels to return 
    * @return A `std::vector` of `PriceLvl` objects, sorted from the highest (best) bid price downwards.
    */
    std::vector<PriceLvl> GetAggBidLvls(uint32_t instrId, size_t numLvls) const {
        std::map<int64_t, PriceLvl> aggBids;
        auto itInstrBooks = books_.find(instrId);
        if (itInstrBooks != books_.end()) {
            for (const auto& pair : itInstrBooks->second) {
                const auto& pb_book = pair.second;
                const auto blvls = pb_book.GetBidLvls(numLvls);
                for (const auto& lvl : blvls) {
                    if (lvl.IsEmpty()) continue;
                    aggBids[lvl.price].size += lvl.size;
                    aggBids[lvl.price].count += lvl.count;
                    aggBids[lvl.price].price = lvl.price;
                }
            }
        }
        std::vector<PriceLvl> res; res.reserve(numLvls); auto it = aggBids.rbegin();
        for (size_t i = 0; i < numLvls && it != aggBids.rend(); ++i, ++it) res.emplace_back(it->second);
        return res;
    }

    /**
     * @brief Retrieves aggregated ask price levels for a specific instrument across all publishers.
     * @param instrId The unique identifier of the financial instrument.
     * @param numLvls The maximum number of aggregated price levels to return.
     * @return A `std::vector` of `PriceLvl` objects, sorted from
     */
    std::vector<PriceLvl> GetAggAskLvls(uint32_t instrId, size_t numLvls) const {
        std::map<int64_t, PriceLvl> aggAsks;
        auto itInstrBooks = books_.find(instrId);
        if (itInstrBooks != books_.end()) {
            for (const auto& pair : itInstrBooks->second) {
                const auto& pb_book = pair.second;
                const auto alvls = pb_book.GetAskLvls(numLvls);
                for (const auto& lvl : alvls) {
                    if (lvl.IsEmpty()) continue;
                    aggAsks[lvl.price].size += lvl.size;
                    aggAsks[lvl.price].count += lvl.count;
                    aggAsks[lvl.price].price = lvl.price;
                }
            }
        }
        std::vector<PriceLvl> res; res.reserve(numLvls); auto it = aggAsks.begin();
        for (size_t i = 0; i < numLvls && it != aggAsks.end(); ++i, ++it) res.emplace_back(it->second);
        return res;
    }

    /**
     * @brief Gets the depth of a specific level in the book for a given instrument and publisher.
     * @param instrId The unique identifier of the financial instrument.
     * @param pubId The unique identifier of the publisher.
     * @param price The price level to check.
     * @param side The side of the book (Bid or Ask).
     * @return The depth of the level at the specified price, or 0 if the level does not exist.
     */
    uint32_t GetLevelDepth(uint32_t instrId, uint16_t pubId, int64_t price, Sd::Type side) const {
        auto itInstrBooks = books_.find(instrId);
        if (itInstrBooks == books_.end()) return 0;

        auto itPubBook = itInstrBooks->second.find(pubId);
        if (itPubBook != itInstrBooks->second.end()) {
            if (side == Sd::Bid) return itPubBook->second.GetBidLevelDepth(price);
            else if (side == Sd::Ask) return itPubBook->second.GetAskLevelDepth(price);
        }
        return 0;
    }

    /**
     * @brief Applies a market by order message to the appropriate book.
     * @param m The MboSingle message containing the order details.
     */
    void Apply(const MboSingle& m) {
        books_[m.instrId][m.pubId].Apply(m);
    }

    /**
     * @brief Processes a synthetic trade for a specific instrument and publisher.
     * @param instrId The unique identifier of the financial instrument.
     * @param pubId The unique identifier of the publisher.
     * @param px The price of the synthetic trade.
     * @param sz The size of the synthetic trade.
     * @param sideAff The side affected by the synthetic trade (Bid or Ask).
     */
    void ProcSynthTrade(uint32_t instrId, uint16_t pubId, int64_t px, uint32_t sz, Sd::Type sideAff) {
        auto itInstrBooks = books_.find(instrId);
        if (itInstrBooks == books_.end()) {
            std::cerr << "Err: Synth trade for non-existent instr " << instrId << ". Ign.\n";
            return;
        }
        auto itPubBook = itInstrBooks->second.find(pubId);
        if (itPubBook == itInstrBooks->second.end()) {
            std::cerr << "Err: Synth trade for non-existent book (Instr: " + std::to_string(instrId) + ", Pub: " + std::to_string(pubId) + "). Ign.\n";
            return;
        }
        itPubBook->second.ProcSynthTrade(px, sz, sideAff);
    }

private:
    /**
     * @brief Type for mapping publisher IDs to their respective books.
     */
    std::unordered_map<uint32_t, std::unordered_map<uint16_t, Book>> books_;
};

/**
 * @brief Parses a line from the MBO input file and returns a MboSingle object.
 * @param line The line to parse.
 * @return A MboSingle object containing the parsed data.
 */
MboSingle ParseMboLine(const std::string& line) {
    MboSingle m;
    std::string t;
    std::istringstream ss(line);

    std::getline(ss, m.tsRecv, ',');
    std::getline(ss, m.tsEvent, ',');
    std::getline(ss, t, ','); m.rtype = static_cast<uint8_t>(std::stoul(t));
    std::getline(ss, t, ','); m.pubId = static_cast<uint16_t>(std::stoul(t));
    std::getline(ss, t, ','); m.instrId = static_cast<uint32_t>(std::stoul(t));
    std::getline(ss, t, ','); m.action = static_cast<Act::Type>(t[0]);
    std::getline(ss, t, ','); m.side = static_cast<Sd::Type>(t[0]);
    std::getline(ss, t, ','); m.price = t.empty() ? UNDEFINED_PRICE : ToNanoPrice(std::stod(t));
    std::getline(ss, t, ','); m.size = static_cast<uint32_t>(std::stoul(t));
    std::getline(ss, t, ','); m.chanId = static_cast<uint8_t>(std::stoul(t));
    std::getline(ss, t, ','); m.orderId = static_cast<uint64_t>(std::stoull(t));
    std::getline(ss, t, ','); m.flags = static_cast<uint8_t>(std::stoul(t));
    std::getline(ss, t, ','); m.tsInDelta = static_cast<int32_t>(std::stol(t));
    std::getline(ss, t, ','); m.sequence = static_cast<uint32_t>(std::stoul(t));
    std::getline(ss, m.symbol);
    return m;
}

/**
 * @brief Writes the header for the Market By Price (MBP) output file.
 * @param os The output stream to write the header to.
 */
void WriteMbpHdr(std::ostream& os) {
    os << ",ts_recv,ts_event,rtype,publisher_id,instrument_id,action,side,depth,price,size,flags,ts_in_delta,sequence,";
    for (int i = 0; i < 10; ++i) {
        os << "bid_px_" << std::setw(2) << std::setfill('0') << i << ",";
        os << "bid_sz_" << std::setw(2) << std::setfill('0') << i << ",";
        os << "bid_ct_" << std::setw(2) << std::setfill('0') << i << ",";
        os << "ask_px_" << std::setw(2) << std::setfill('0') << i << ",";
        os << "ask_sz_" << std::setw(2) << std::setfill('0') << i << ",";
        os << "ask_ct_" << std::setw(2) << std::setfill('0') << i;
        if (i < 9) os << ",";
    }
    os << ",symbol,order_id\n";
}

/**
 * @brief Writes a row to the Market By Price (MBP) output file.
 * @param os The output stream to write the row to.
 * @param mi The MboSingle object containing the data for the row.
 * @param bl The aggregated bid price levels.
 * @param al The aggregated ask price levels.
 * @param rIdx The index of the row being written.
 * @param depth_val The depth value for the row.
 */
void WriteMbpRow(std::ostream& os, const MboSingle& mi, const std::vector<PriceLvl>& bl, const std::vector<PriceLvl>& al, int rIdx, uint32_t depth_val) {
    os << rIdx << ",";
    os << mi.tsRecv << "," << mi.tsEvent << "," << static_cast<uint32_t>(10) << "," << static_cast<uint32_t>(mi.pubId) << ","
       << static_cast<uint32_t>(mi.instrId) << "," << mi.action << "," << mi.side << "," << depth_val << ",";

    if (mi.price == UNDEFINED_PRICE) os << ",";
    else os << std::fixed << std::setprecision(9) << ToDblPrice(mi.price) << ",";

    os << static_cast<uint32_t>(mi.size) << "," << static_cast<uint32_t>(mi.flags) << "," << static_cast<int32_t>(mi.tsInDelta) << ","
       << static_cast<uint32_t>(mi.sequence) << ",";

    for (size_t i = 0; i < 10; ++i) {
        if (i < bl.size() && bl[i]) {
            os << std::fixed << std::setprecision(9) << ToDblPrice(bl[i].price) << "," << static_cast<uint32_t>(bl[i].size) << "," << static_cast<uint32_t>(bl[i].count) << ",";
        } else {
            os << ",0,0,";
        }
        if (i < al.size() && al[i]) {
            os << std::fixed << std::setprecision(9) << ToDblPrice(al[i].price) << "," << static_cast<uint32_t>(al[i].size) << "," << static_cast<uint32_t>(al[i].count);
        } else {
            os << ",0,0";
        }
        if (i < 9) os << ",";
    }
    os << "," << mi.symbol << "," << static_cast<uint64_t>(mi.orderId) << "\n";
}

/**
 * @brief Main function to reconstruct the Market By Price (MBP) from MBO input data.
 * @param argc The number of command line arguments.
 */
int main(int argc, char* argv[]) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <mbo_input_file.csv>\n";
        return 1;
    }

    std::string mboFilePath = argv[1];
    std::string mbpOutPath = "output.csv";
    std::ifstream mboIs(mboFilePath);
    if (!mboIs.is_open()) {
        std::cerr << "Error: Open MBO file: " + mboFilePath + "\n";
        return 1;
    }
    std::ofstream mbpOs(mbpOutPath);
    if (!mbpOs.is_open()) {
        std::cerr << "Error: Open MBP file: " + mbpOutPath + "\n";
        return 1;
    }
  
    WriteMbpHdr(mbpOs);
    Market market;
    std::string line;
    int mbpRowIdx = 0;
    std::unordered_map<uint64_t, MboSingle> pendingTFs;

    std::getline(mboIs, line);

    while (std::getline(mboIs, line)) {
        MboSingle m = ParseMboLine(line);
        uint32_t current_depth = 0;

        if (m.action == Act::Trade && m.side == Sd::None) {
            current_depth = 0;
        } else if (m.action == Act::Trade || m.action == Act::Fill) {
            pendingTFs[m.orderId] = m;
            current_depth = 0;
        } else if (m.action == Act::Cancel) {
            auto itPend = pendingTFs.find(m.orderId);
            if (itPend != pendingTFs.end()) {
                MboSingle origTFM = std::move(itPend->second);
                pendingTFs.erase(itPend);

                Sd::Type sideAff;
                if (origTFM.side == Sd::Ask) sideAff = Sd::Bid;
                else if (origTFM.side == Sd::Bid) sideAff = Sd::Ask;
                else {
                    std::cerr << "Warn: T/F in TFC for ID " + std::to_string(m.orderId) + " Side::None. Skipping synth trade.\n";
                    current_depth = 0;
                    auto bl = market.GetAggBidLvls(m.instrId, 10);
                    auto al = market.GetAggAskLvls(m.instrId, 10);
                    WriteMbpRow(mbpOs, m, bl, al, mbpRowIdx++, current_depth);
                    continue;
                }
                
                try {
                    market.ProcSynthTrade(m.instrId, m.pubId, origTFM.price, origTFM.size, sideAff);
                } catch (const std::exception& e) {
                    std::cerr << "Err synth trade ID " + std::to_string(m.orderId) + ": " + e.what() + "\n";
                }
                current_depth = market.GetLevelDepth(m.instrId, m.pubId, origTFM.price, sideAff);

            } else {
                market.Apply(m);
                current_depth = market.GetLevelDepth(m.instrId, m.pubId, m.price, m.side);
            }
        } else {
            market.Apply(m);
            if (m.action == Act::Add || m.action == Act::Modify) {
                current_depth = market.GetLevelDepth(m.instrId, m.pubId, m.price, m.side);
            } else if (m.action == Act::Clear) {
                current_depth = 0;
            }
        }

        auto bl = market.GetAggBidLvls(m.instrId, 10);
        auto al = market.GetAggAskLvls(m.instrId, 10);
        WriteMbpRow(mbpOs, m, bl, al, mbpRowIdx++, current_depth);
    }

    mboIs.close();
    mbpOs.close();

    
    std::cout << "MBP-10 reconstruction complete. Output saved to " + mbpOutPath + "\n";
    return 0;
}