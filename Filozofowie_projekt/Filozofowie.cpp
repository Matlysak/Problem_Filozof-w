#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <ncurses.h>
#include <random>
#include <atomic>
#include <memory>
#include <condition_variable>

using namespace std;

const int NUM_PHILOSOPHERS = 5;
atomic<bool> running(true);
vector<string> philosopher_names = {"Yoda", "Gandalf", "Socrates", "Confucius", "Plato"};

enum class PhilosopherState { THINKING, HUNGRY, EATING };

class DiningPhilosophers {
protected:
    vector<PhilosopherState> states;
    vector<int> meals_eaten;
    vector<mutex> chopsticks;
    vector<int> current_chopstick_users;  
    vector<int> last_chopstick_users;  
    mutex display_mutex;
    
    random_device rd;
    mt19937 gen;
    uniform_int_distribution<> eat_dist;
    uniform_int_distribution<> think_dist;

    void display_status(int id) {
        lock_guard<mutex> lock(display_mutex);
        clear();
        
        printw("=== Dining Philosophers Problem ===\n\n");
        
        for (int i = 0; i < NUM_PHILOSOPHERS; ++i) {
            printw("%s: ", philosopher_names[i].c_str());
            
            switch(states[i]) {
                case PhilosopherState::THINKING:
                    attron(COLOR_PAIR(1));
                    printw("THINKING");
                    attroff(COLOR_PAIR(1));
                    break;
                case PhilosopherState::HUNGRY:
                    attron(COLOR_PAIR(2));
                    printw("HUNGRY ");
                    attroff(COLOR_PAIR(2));
                    break;
                case PhilosopherState::EATING:
                    attron(COLOR_PAIR(3));
                    printw("EATING ");
                    attroff(COLOR_PAIR(3));
                    break;
            }
            
            printw(" (Meals: %d)\n", meals_eaten[i]);
        }
        
        printw("\nChopsticks:\n");
        for (int i = 0; i < NUM_PHILOSOPHERS; ++i) {
            printw(" [%d] ", i+1);
            if (current_chopstick_users[i] != -1) {
                attron(COLOR_PAIR(3));
                printw("Held by %s", philosopher_names[current_chopstick_users[i]].c_str());
                attroff(COLOR_PAIR(3));
            } else {
                attron(COLOR_PAIR(1));
                printw("Free");
                attroff(COLOR_PAIR(1));
                
                if (last_chopstick_users[i] != -1) {
                    printw(" (last used by %s)", philosopher_names[last_chopstick_users[i]].c_str());
                }
            }
            printw("\n");
        }
        
        refresh();
    }

public:
    DiningPhilosophers() : 
        states(NUM_PHILOSOPHERS, PhilosopherState::THINKING),
        meals_eaten(NUM_PHILOSOPHERS, 0),
        chopsticks(NUM_PHILOSOPHERS),
        current_chopstick_users(NUM_PHILOSOPHERS, -1),
        last_chopstick_users(NUM_PHILOSOPHERS, -1),
        gen(rd()),
        eat_dist(1, 4),
        think_dist(2, 5) {}
        
    virtual ~DiningPhilosophers() = default;
    virtual void philosophize(int id) = 0;
    
    void run() {
        vector<thread> philosophers;
        for (int i = 0; i < NUM_PHILOSOPHERS; ++i) {
            philosophers.emplace_back(&DiningPhilosophers::philosophize, this, i);
        }
        
        while (running) {
            this_thread::sleep_for(chrono::seconds(1));
        }
        
        for (auto& t : philosophers) {
            t.join();
        }
    }
};

class DeadlockVersion : public DiningPhilosophers {
public:
    void philosophize(int id) override {
        int left = id;
        int right = (id + 1) % NUM_PHILOSOPHERS;
        
        while (running) {
            states[id] = PhilosopherState::HUNGRY;
            display_status(id);
            
            chopsticks[left].lock();
            {
                lock_guard<mutex> lock(display_mutex);
                last_chopstick_users[left] = current_chopstick_users[left];
                current_chopstick_users[left] = id;
            }
            display_status(id);
            this_thread::sleep_for(chrono::milliseconds(100));
            
            chopsticks[right].lock();
            {
                lock_guard<mutex> lock(display_mutex);
                last_chopstick_users[right] = current_chopstick_users[right];
                current_chopstick_users[right] = id;
            }
            
            states[id] = PhilosopherState::EATING;
            meals_eaten[id]++;
            display_status(id);
            
            this_thread::sleep_for(chrono::seconds(eat_dist(gen)));
            
            {
                lock_guard<mutex> lock(display_mutex);
                last_chopstick_users[left] = current_chopstick_users[left];
                current_chopstick_users[left] = -1;
                last_chopstick_users[right] = current_chopstick_users[right];
                current_chopstick_users[right] = -1;
            }
            chopsticks[left].unlock();
            chopsticks[right].unlock();
            
            states[id] = PhilosopherState::THINKING;
            display_status(id);
            
            this_thread::sleep_for(chrono::seconds(think_dist(gen)));
        }
    }
};

class StarvationVersion : public DiningPhilosophers {
public:
    void philosophize(int id) override {
        int left = id;
        int right = (id + 1) % NUM_PHILOSOPHERS;
        
        while (running) {
            states[id] = PhilosopherState::HUNGRY;
            display_status(id);
            
            if (id == 0) {
                if (!chopsticks[right].try_lock()) {
                    this_thread::sleep_for(chrono::milliseconds(think_dist(gen) * 2));
                    continue;
                }
                
                {
                    lock_guard<mutex> lock(display_mutex);
                    last_chopstick_users[right] = current_chopstick_users[right];
                    current_chopstick_users[right] = id;
                }
                
                this_thread::sleep_for(chrono::milliseconds(200));
                if (!chopsticks[left].try_lock()) {
                    chopsticks[right].unlock();
                    {
                        lock_guard<mutex> lock(display_mutex);
                        last_chopstick_users[right] = current_chopstick_users[right];
                        current_chopstick_users[right] = -1;
                    }
                    this_thread::sleep_for(chrono::milliseconds(think_dist(gen) * 2));
                    continue;
                }
                
                {
                    lock_guard<mutex> lock(display_mutex);
                    last_chopstick_users[left] = current_chopstick_users[left];
                    current_chopstick_users[left] = id;
                }
            } 
            else {
                chopsticks[left].lock();
                {
                    lock_guard<mutex> lock(display_mutex);
                    last_chopstick_users[left] = current_chopstick_users[left];
                    current_chopstick_users[left] = id;
                }

                this_thread::sleep_for(chrono::milliseconds(50));
                
                chopsticks[right].lock();
                {
                    lock_guard<mutex> lock(display_mutex);
                    last_chopstick_users[right] = current_chopstick_users[right];
                    current_chopstick_users[right] = id;
                }
            }
            
            states[id] = PhilosopherState::EATING;
            meals_eaten[id]++;
            display_status(id);
            
            this_thread::sleep_for(chrono::seconds(eat_dist(gen)));
            
            {
                lock_guard<mutex> lock(display_mutex);
                last_chopstick_users[left] = current_chopstick_users[left];
                current_chopstick_users[left] = -1;
                last_chopstick_users[right] = current_chopstick_users[right];
                current_chopstick_users[right] = -1;
            }
            chopsticks[left].unlock();
            chopsticks[right].unlock();
            
            states[id] = PhilosopherState::THINKING;
            display_status(id);

            this_thread::sleep_for(chrono::seconds(id == 0 ? think_dist(gen) * 2 : think_dist(gen)));
        }
    }
};

class CorrectVersion : public DiningPhilosophers {
private:
    mutex arbitrator;
    
public:
    void philosophize(int id) override {
        int left = id;
        int right = (id + 1) % NUM_PHILOSOPHERS;
        
        while (running) {
            states[id] = PhilosopherState::HUNGRY;
            display_status(id);
            
            arbitrator.lock();
            
            chopsticks[left].lock();
            {
                lock_guard<mutex> lock(display_mutex);
                last_chopstick_users[left] = current_chopstick_users[left];
                current_chopstick_users[left] = id;
            }
            chopsticks[right].lock();
            {
                lock_guard<mutex> lock(display_mutex);
                last_chopstick_users[right] = current_chopstick_users[right];
                current_chopstick_users[right] = id;
            }
            
            states[id] = PhilosopherState::EATING;
            meals_eaten[id]++;
            display_status(id);
            
            this_thread::sleep_for(chrono::seconds(eat_dist(gen)));
            
            {
                lock_guard<mutex> lock(display_mutex);
                last_chopstick_users[left] = current_chopstick_users[left];
                current_chopstick_users[left] = -1;
                last_chopstick_users[right] = current_chopstick_users[right];
                current_chopstick_users[right] = -1;
            }
            chopsticks[left].unlock();
            chopsticks[right].unlock();
            
            arbitrator.unlock();
            
            states[id] = PhilosopherState::THINKING;
            display_status(id);
            
            this_thread::sleep_for(chrono::seconds(think_dist(gen)));
        }
    }
};

void init_ncurses() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
}

void cleanup_ncurses() {
    endwin();
}

int main() {
    init_ncurses();
    
    clear();
    printw("=== Problem Filozofow - Wybierz wersje ===\n\n");
    printw("1. Wersja z zakleszczeniem\n");
    printw("2. Wersja z zaglodzeniem\n");
    printw("3. Poprawne rozwiazanie (z arbitrem)\n");
    printw("\nWybierz (1-3): ");
    refresh();
    
    int choice = 0;
    while (choice < '1' || choice > '3') {
        choice = getch();
    }
    
    unique_ptr<DiningPhilosophers> dinner;
    switch(choice) {
        case '1':
            dinner = make_unique<DeadlockVersion>();
            break;
        case '2':
            dinner = make_unique<StarvationVersion>();
            break;
        case '3':
            dinner = make_unique<CorrectVersion>();
            break;
    }
    
    clear();
    refresh();
    
    thread dinner_thread([&dinner]() {
        dinner->run();
    });
    
    while (running) {
        this_thread::sleep_for(chrono::seconds(1));
    }
    
    dinner_thread.join();
    cleanup_ncurses();
    return 0;
}