package main

import (
	"fmt"
	"sync"
	"time"
)

func main() {
	ch := make(chan int, 10)
	var dis_mux sync.Mutex
	var waitGroup = sync.WaitGroup{}
	const test_iter = 100

	t1 := time.Now()
	for k := 0; k < test_iter; k++ {
		waitGroup.Add(200)
		for i := 0; i < 100; i++ {
			go func(id int) {
				dis_mux.Lock()
				fmt.Printf("[INFO] consumer %v\n", id)
				dis_mux.Unlock()

				val := <- ch

				dis_mux.Lock()
				fmt.Printf("[INFO] consumer %v get data %v done\n", id, val)
				dis_mux.Unlock()
				waitGroup.Done()
			}(i)

			go func(id int) {
				dis_mux.Lock()
				fmt.Printf("[INFO] producer %v\n", id)
				dis_mux.Unlock()

				ch <- id

				dis_mux.Lock()
				fmt.Printf("[INFO] producer %v done\n", id)
				dis_mux.Unlock()
				waitGroup.Done()
			}(i)
		}
/*
		for i := 0; i < 100; i++ {
			go func(id int) {
				dis_mux.Lock()
				fmt.Printf("[INFO] producer %v\n", id)
				dis_mux.Unlock()

				ch <- id

				dis_mux.Lock()
				fmt.Printf("[INFO] producer %v done\n", id)
				dis_mux.Unlock()
				waitGroup.Done()
			}(i)
		}
		*/

		waitGroup.Wait()
	}

	t2 := time.Since(t1)
	fmt.Println("[TIME] ", t2 / test_iter)
}
