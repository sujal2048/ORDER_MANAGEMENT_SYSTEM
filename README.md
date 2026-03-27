# ORDER_MANAGEMENT_SYSTEM
# ORDER MATCHING ENGINE explanation

The engine must store orders in a hash map for O(1) access by order ID. Handle and matching according to rules: BUY matches lowest SELL, SELL matches highest BUY, price-time priority. Partial fills allowed. Also maintaining net position per client, and PnL.Implementing concurrency (producer-consumer model) to be robust. The code is compiled in RHEL 9, and used websocket library  Beast (Boost.Beast). Also  handled concurrent requests: websocket server will accept multiple connections, each sending commands. We'll have a matching engine that processes commands from a queue, and websocket threads push commands into the queue.The engine runs in a separate thread, consuming commands, performing matching, and sending responses back via websocket (maybe per client).we have stored orders in a hash map by ID for quick lookup for modify/cancel. For matching, we had maintain order books: one for BUY and one for SELL, each sorted by price-time priority. For BUY side: highest price first (descending), and within same price, earliest time first (FIFO). For SELL side: lowest price first (ascending), and within same price, earliest time first. We have also support partial fills: when matching, we have partially filled an order, reducing its quantity, and possibly keep the remaining in the book. Also we need to maintain net position per client: net quantity (sum of filled buys - sum of filled sells). PnL: profit/loss. Usually PnL = (sell price - buy price) * quantity. . We have kept it simple: track realized PnL from each match: when a buy matches a sell, we compute (sell price - buy price) * quantity, and add to total PnL if positive (profit) else negative. Alternatively, we have maintain per client PnL. we maintain per client net position and PnL (realized). When a match occurs, we update client's position and PnL accordingly. For net position, we can compute as total bought quantity minus total sold quantity. When a buy matches a sell, the buy side client's position increases by filled quantity (since they are buying), sell side client's position decreases (they are selling). For PnL, for each match, we can credit/debit accordingly.

# Data_Structures Explanation

Order: id, client_id, side (BUY/SELL), price, quantity (remaining quantity), timestamp (for time priority). We'll store in a hash map: unordered_map<int, Order> (by order ID). when we modify an order, we need to update the book and map. When cancelling, remove from map and book.

OrderBook: we have two priority structures: buy_orders and sell_orders. For price-time priority, we have used a data structure that allows efficient insertion, removal, and retrieval of best order. Typically, a map of price to a list of orders (FIFO). For BUY side: we need highest price first, so we can use a std::map with descending key (or use greater comparator). For SELL side: std::map with ascending key. Within each price, we need a queue of orders (or list) that maintains insertion order. When we need to match, we take the best price (first element in map), then take the first order in the list at that price. For partial fills, we might reduce quantity of that order; if fully filled, we remove it from the list and if list becomes empty, remove the price entry. For cancellations/modifications, we need to locate the order in the book quickly. Since we have the order ID, we can get the order from the map, but we also need to know which price level and which position in the list to remove it efficiently. We can store in the order object an iterator to the list node (if using std::list) and a reference to the price level. 

# Data structures:

unordered_map<int, Order> orders; // key: order id

For buy side: map<int, set<OrderTimeKey>>, where map key is price (int), and value is a set of (timestamp, order_id). We need a comparator that sorts by timestamp ascending (FIFO). Since timestamp can be same? Use tie-breaker with order_id. So we define a struct OrderTimeKey { long long ts; int id; } and define operator<.

For sell side: similarly map<int, set<OrderTimeKey>>, but for sell we need lowest price first, so map key ascending. But we also need to get best sell: the first element of map (lowest price). For buy best: last element of map (highest price). But map's begin() gives smallest key. So we can use rbegin() for buy side. Or we can store in descending order: map<int, set<...>, greater<int>> for buy side, then begin() gives highest price. That may be easier. We'll do that: using BuyMap = std::map<int, std::set<OrderTimeKey>, std::greater<int>>; SellMap = std::map<int, std::set<OrderTimeKey>>; (ascending default).


# matching logic
When a new BUY order arrives, we try to match with existing SELL orders. We iterate while best sell exists and best sell price <= buy price. For each match, we take the earliest sell order at that price (first in the set). The matched quantity is min(buy.remaining, sell.remaining). Then we update both orders: reduce remaining quantity, update positions and PnL for clients. If sell order is fully filled, remove from its set and from orders map. If buy order is fully filled, break loop (no need to add to book). If after matching, buy order still has remaining quantity, we add it to the buy book (insert into buy map at its price with timestamp). For new SELL order, similar but matching with BUY orders (best buy price >= sell price)

2.For modify: we need to find order, remove from current book (if it was in book), update its price/quantity, then re-match if it's a new order? Actually modify can change price or quantity.
in matching engines, modify retains its time priority? Typically, modify can be treated as cancel + new, but sometimes the time priority resets. 
### We'll treat as cancel + new: we remove the order from the book (if it was there) and then create a new order with the same ID but new parameters and current timestamp.
We'll also update the order map accordingly. For cancel: remove from book and from orders map. For PRINT: output current state of order book and positions? Could be debugging. We'll implement PRINT that prints buy and sell book levels

# concurrency

The matching engine runs in a separate thread, consuming commands from a thread-safe queue. WebSocket sessions (multiple) produce commands into the queue. The engine processes commands one by one, and for each command, it may produce a response (e.g., ack, fill notification, book state). The engine needs to send responses back to the appropriate client session. We need to associate each command with the connection that sent it, so that we can send response back. We can store in the command a weak_ptr or a reference to the session, or we can have a global map from client_id to session? But multiple sessions per client? Probably not. Since we have client_id in the request, we can send response to the same session. We'll need to pass the session's write queue. But to avoid coupling, we can have the command contain a callback function to send response. We'll use std::function<void(const std::string&)> to send message back. That way the engine doesn't need to know about sessions. When a session receives a command, it creates a Command object with the parsed data and a lambda that captures the session's write function (e.g., via weak_ptr). Then it pushes to the queue. The engine processes, and when done, invokes the callback with the response.

We have used a thread-safe queue. We can use a lock-based queue with condition variable. We'll implement a simple concurrent queue.

#BUILD 
--expecting user to be in /home/user/
mkdir build
cd /home/user/build
rm -rf *
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)


After successfull completion start the server
./order_matching_engine
