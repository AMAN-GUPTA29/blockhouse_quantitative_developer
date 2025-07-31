README.txt
Blockhouse Quantitative Developer Work Trial Task: Reconstructing MBP-10 data from MBO data

1. Overview
This project delivers a C++ solution designed to reconstruct a Level 10 Market By Price (MBP-10) order book from a real-time stream of Market By Order (MBO) data. In the context of high-frequency trading, understanding the real-time liquidity and microstructure of an asset is paramount. While MBO provides granular detail of every order-level action, models often require a consolidated view like MBP-10 for efficiency. The core challenge lies in processing the vast volume and velocity of MBO data efficiently to maintain an accurate and up-to-date MBP-10 representation, which is critical for execution quality optimization and short-term alpha generation.

2. Core Concepts: MBO vs. MBP-10 & The Reconstruction Goal

   a. **Market By Order (MBO) Data (Input):**
      MBO data represents the most granular view of exchange activity. It provides a timestamped stream of individual order actions (e.g., new order, modification, cancellation, trade/fill). Each entry typically includes an order ID, price, quantity, side (Bid/Ask), and action type. While incredibly detailed, processing raw MBO data in real-time for consolidated views can be computationally intensive due to its high volume.

   b. **Market By Price - Top 10 Levels (MBP-10) Data (Output):**
      MBP-10 represents a consolidated view of the order book by price level, showing the aggregated quantity available at each of the best 10 bid prices and best 10 ask prices. It abstracts away individual order IDs, providing a simpler, aggregated snapshot of market depth. This view is more suitable for consumption by trading models as it provides liquidity insights without the overhead of individual order management.

   c. **The Reconstruction Goal:**
      The primary goal of this task is to continuously compute and maintain the MBP-10 state based on the incoming MBO stream. This involves:
      - Parsing MBO messages efficiently.
      - Accurately updating an internal representation of the order book based on each action.
      - Periodically (or upon significant changes) deriving the top 10 bid and ask price levels with their aggregated quantities.
      The key is to perform these operations with minimal latency and memory footprint to keep up with high-frequency market updates.

3. Problem-Specific Reconstruction Rules

The assignment specifies particular rules for processing certain MBO actions to derive the correct MBP-10 state. These rules are directly implemented in the `main` function's processing loop:

   a. **Initial Order Book State:**
      - The very first row of the `mbo.csv` input, often a "clear[R]" action (header row in sample data), must be ignored.
      - **How Handled:** The code explicitly calls `std::getline(mboIs, line);` once *before* the main processing loop to discard the header/initial clear row, ensuring reconstruction begins with an assumed empty order book.

   b. **Combined T, F, C Actions for Trades:**
      - In typical MBO, `[T]rade` and `[F]ill` actions alone do not directly affect the *limit order book* (they represent executed orders). Only subsequent `[C]ancel` actions might affect a remaining quantity in the book.
      - For this assignment, a specific sequence `[T]rade -> [F]ill -> [C]ancel` (often related to a single trade event) is to be combined into a *single logical `[T]` action in the MBP-10 output that reflects a change in the order book*.
      - **Crucial Logic for Quantity Adjustment:** If the `[T]` action is on the ASK side (indicating an incoming buy order hitting an existing sell order), the quantity change derived from the subsequent `[C]` action must be applied to the **BID side** of the order book. Conversely, if the `[T]` action is on the BID side, the change is applied to the **ASK side**. This reinterpretation ensures the order book accurately reflects the depletion of liquidity that was *matched* by the trade.
      - **How Handled:**
        - A `std::unordered_map<uint64_t, MboSingle> pendingTFs;` is used to temporarily store `[T]` or `[F]` messages keyed by `orderId`.
        - When a `[C]` (Cancel) action is encountered, the code checks if a corresponding `[T]` or `[F]` for the same `orderId` exists in `pendingTFs`.
        - If found, the original `T`/`F` message's `side` (`origTFM.side`) is used to determine the `sideAff` (side affected in the book): if `origTFM.side` was `Ask`, the `sideAff` is `Bid`, and vice-versa.
        - The `market.ProcSynthTrade` method is then called using the original `T`/`F`'s price (`origTFM.price`) and size (`origTFM.size`), applying the effect to the calculated `sideAff`. The `Cancel` message itself is then processed for output to represent this combined trade.

   c. **Ignoring Neutral-Sided Trades:**
      - If a `[T]` (Trade) action has its `side` field marked as 'N' (Neutral), this specific trade should *not* cause any alteration to the order book.
      - **How Handled:** The `main` loop explicitly checks `if (m.action == Act::Trade && m.side == Sd::None)`. If this condition is met, `current_depth` is set to 0, and the trade does not trigger any `market.Apply` or `market.ProcSynthTrade` call, effectively ignoring it for order book updates.

4. Build Instructions (Windows using MinGW/MSYS2)

The project uses a `Makefile` for streamlined compilation. Ensure you have MinGW or MSYS2 installed, with `g++` and `make` (or `mingw32-make`) added to your system's PATH.

   a. **Open Terminal:** Navigate to the project root (e.g., `cd C:\path\to\your\project`).
   b. **Build:** Execute the `make` command:
      ```bash
      mingw32-make
      # Or, if 'make' is directly in your PATH:
      # make
      ```
      This command triggers the compilation of `reconstruction.cpp` into an object file (`reconstruction.o`), followed by linking `reconstruction.o` with necessary libraries to create the final executable, `reconstruction_aman.exe`. This standard process allows for efficient incremental builds.
   c. **Clean (Optional):** Remove generated build artifacts (`.o` files and `.exe`).
      ```bash
      mingw32-make clean
      # Or:
      # make clean
      ```

5. Usage

   a. **Execute the Program:** Run the compiled executable, passing the MBO data file (`mbo.csv`) as a command-line argument.
      ```bash
      ./reconstruction_aman.exe mbo.csv
      ```
      **Note:** `mbo.csv` is an input data file, not part of the compilation process managed by the `Makefile`.
   b. **Save Output:** Redirect standard output to a file to capture the reconstructed MBP-10 data. By default, the output will be saved to `output.csv`. To redirect to a custom file (e.g., `mbp_output.csv`):
     
      Ensure `mbo.csv` is in the execution directory or provide its full path.

6. Technical Implementation Details & Optimizations

Performance is paramount for this task. The following sections detail the architectural and coding choices made to achieve correctness, speed, and efficiency.

   a. **Order Book Data Structures (`Book` class):**
      To efficiently manage and query price levels for each instrument and publisher, the `Market` class uses nested `unordered_map`s (`std::unordered_map<uint32_t, std::unordered_map<uint16_t, Book>> books_`). Each `Book` instance then maintains its Bid and Ask sides:
      - `std::map<int64_t, std::list<MboSingle>> bids_;`
      - `std::map<int64_t, std::list<MboSingle>> offers_;`
      - `std::unordered_map<uint64_t, PxAndSd> ordsById_;` for quick order lookup by ID.

      **Why `std::map` with `std::list`?**
      - `std::map<int64_t, ...>`: `std::map` automatically keeps its elements sorted by key (`price` in `int64_t` nanoseconds). This is crucial for efficiently retrieving the **top 10 levels** on both Bid (using reverse iterators for descending prices) and Ask (using forward iterators for ascending prices) sides, which are the primary output requirements. The `int64_t` price representation (`ToNanoPrice`) avoids floating-point precision issues in map keys.
      - `std::list<MboSingle>` (LvlOrdsInQ): Orders at the same price level are stored in a `std::list`. A `std::list` provides efficient `O(1)` insertion and deletion of elements once an iterator to the element is obtained. This is vital for `Add`, `Cancel`, and `Modify` operations that involve individual orders within a price level, maintaining time priority if needed.
      - `std::unordered_map<uint64_t, PxAndSd>` (`ordsById_`): This hash map provides `O(1)` average-case lookup of an order's `price` and `side` given its `orderId`. This is critical for `Cancel` and `Modify` operations, where the original price and side of an order need to be quickly found.

   b. **Efficient CSV Parsing (`ParseMboLine` function):**
      Given the high volume of MBO data, parsing efficiency is critical.
      - **Strategy:** The `ParseMboLine` function avoids heavyweight string stream operations per field. Instead, it uses `std::getline` with a comma delimiter to efficiently extract substrings representing each field. Numerical conversions are then performed using optimized functions like `std::stoul`, `std::stol`, `std::stoull`, and `std::stod`.
      - **Price Conversion:** Prices are stored internally as `int64_t` nanoseconds (`PRICE_SCALE = 1e9`) using `ToNanoPrice` and converted back to `double` for output using `ToDblPrice`. This prevents floating-point precision issues that can arise from direct `double` comparisons and storage in map keys, ensuring accurate order book state.
      - **I/O Optimization:** `std::ios_base::sync_with_stdio(false);` and `std::cin.tie(NULL);` are used at the beginning of `main`. These lines disable synchronization between C++ iostreams and the C standard I/O library and untie `cin` from `cout`, respectively. This significantly boosts input/output performance for large datasets by reducing overhead.
      **Why these choices?** Minimizing string allocations, copies, and I/O overhead directly reduces CPU cycles and memory bandwidth consumption, leading to faster overall processing of the MBO stream.

   c. **Compiler Optimizations (`CXXFLAGS` in Makefile):**
      The `Makefile` employs a robust set of compiler flags to maximize executable performance:
      - `-std=c++17`: Ensures the code is compiled with the C++17 standard, enabling access to modern language features and standard library optimizations.
      - `-Wall`: Activates all commonly used warning messages. This is a best practice for identifying potential coding errors, undefined behavior, and non-portable constructs early in the development cycle, contributing to code correctness and robustness.
      - `-O3`: This is a high level of optimization that instructs the compiler to apply aggressive optimizations. This includes function inlining, loop unrolling, dead code elimination, common subexpression elimination, and various instruction scheduling optimizations, all aimed at reducing execution time.
      - `-flto`: (Link Time Optimization) This powerful flag enables the compiler and linker to perform whole-program analysis and optimization. Instead of optimizing each source file in isolation, LTO allows cross-module optimizations (e.g., inlining functions across different `.cpp` files), often leading to significant performance gains by generating a more cohesive and efficient final executable.
      - `-march=native`: This instructs the compiler to generate machine code specifically optimized for the CPU architecture of the machine on which the compilation is performed. It enables the use of specific instruction sets (like SSE, AVX, etc.) available on the host processor, potentially yielding substantial speedups for numerical and data processing tasks.
      **Why these choices?** These flags are strategically selected to generate highly optimized machine code, directly targeting the "Speed" evaluation criterion by allowing the compiler to make the most efficient use of the underlying hardware and program structure.

   d. **Memory Management & Data Handling:**
      While `std::map` and `std::unordered_map` involve dynamic memory allocation for their nodes, their efficient underlying implementations and the design choices for `PriceLvl` and `MboSingle` (using fixed-size integers where possible) help manage memory. The `std::list` used for orders within a price level offers `O(1)` deletion without reallocating the entire sequence.

   e. **Coding Style & Readability:**
      The code adheres to a consistent coding style, utilizing meaningful variable and function names (e.g., `tsRecv`, `instrId`, `action`), clear function separation (e.g., `ParseMboLine`, `WriteMbpHdr`, `Book::Apply`), and comments for complex logic (e.g., `ToDblPrice`, `ToNanoPrice` functions). This ensures the code is maintainable, interpretable, and clean, aligning with the "Coding Style" evaluation criterion.

7. Challenges and Lessons Learned

   a. **Data Parsing and Price Precision:**
      - **Challenge:** Accurately parsing diverse MBO `action` types and handling floating-point prices reliably was an initial challenge. Direct `double` comparisons and usage as map keys can lead to subtle precision errors.
      - **Solution:** The implementation of `ToNanoPrice` and `ToDblPrice` functions, coupled with internal `int64_t` storage for prices, effectively mitigates floating-point precision issues, ensuring accurate price-level aggregation and comparisons.

   b. **Interpreting T-F-C Sequence:**
      - **Challenge:** The assignment's specific rule for combining `T`, `F`, and `C` actions, particularly in determining the *opposite* book side for liquidity depletion, was complex. It required careful state management and logic to correctly track pending trades.
      - **Solution:** The `pendingTFs` hash map provides an efficient way to store and retrieve pending `T`/`F` messages by `orderId` when a `C` action arrives. The logic then determines the correct side to apply the synthetic trade based on the original `T`/`F` message's side, correctly reflecting the assignment's rule.

   c. **Performance Bottleneck Identification:**
      - **Challenge:** Ensuring the application could keep up with high-frequency data.
      - **Solution:** Adopting `std::ios_base::sync_with_stdio(false);` and `std::cin.tie(NULL);` for I/O, along with careful string parsing without excessive `std::stringstream` usage, were key. The aggressive compiler flags chosen in the `Makefile` also play a significant role in achieving optimal runtime performance.

   d. **Future Optimizations:**
      - For even greater performance in extremely high-throughput scenarios, custom fixed-size allocators for `std::list` nodes within `Book` or a completely custom order book data structure that avoids dynamic allocations (e.g., using arrays and indices) could be explored to minimize cache misses and allocation overhead.
      - While `std::map` offers `O(log N)`, a `std::unordered_map` for price levels could offer `O(1)` average case, but would lose sorted order, requiring a separate mechanism to retrieve top 10 levels. This trade-off wasn't taken to prioritize simplicity and direct `std::map` benefits for sorted access.
