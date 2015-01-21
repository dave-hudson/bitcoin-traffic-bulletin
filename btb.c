/*
 * btb.c
 *	Transaction processing simulation used for the hashingit.com article
 *	"Bitcoin Traffic Bulletin".
 *
 * Copyright (C) 2015 David Hudson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define NEGATIVE_ORDERS 1
#define POSITIVE_ORDERS 10
#define NUM_BUCKETS_PER_ORDER 1000
#define NUM_BUCKETS (NUM_BUCKETS_PER_ORDER * (POSITIVE_ORDERS + NEGATIVE_ORDERS))

/*
 * Transaction record.
 */
struct transaction {
	struct transaction *next;		/* Next transaction in the pending list */
	struct transaction *prev;		/* Previous transaction in the pending list */
	int size;				/* Size of the transaction in bytes */
	double fee;				/* Fee associated with the transaction */
	double time;				/* Time at which this transaction was generated */
};

long int buckets[NUM_BUCKETS];
int smallest_bucket = NUM_BUCKETS;
int largest_bucket = 0;
long long int num_results = 0LL;

/*
 * Details of the pending transactions.
 */
struct transaction *pending_head = NULL;
struct transaction *pending_tail = NULL;
int pending_transactions = 0;
double next_transaction_secs = 0.0;

/*
 * Fast caching allocator.
 */
struct transaction *cache_head = NULL;

/*
 * sim_pp()
 *	Simulate one time period of a Poisson process.
 */
static double sim_pp(double rate)
{
	/*
	 * rand() isn't an ideal function here but if we go out of our way to fold in
	 * some entropy we're ok.
	 */
	double r = (double)rand() / (((double)RAND_MAX) + 1.0);

	return (double)(-log(1.0 - r) / rate);
}

/*
 * sim_transactions()
 *	Simulate the number of transactions arriving in "block_duration" seconds.
 */
int sim_transactions(double block_end_secs, double tps)
{
	int transactions = 0;

	while (1) {
		/*
		 * Given a start time and a block duration see if the next transaction actually fits into
		 * that window.  If it doesn't then there are no new transactions.
		 */
		if (block_end_secs < next_transaction_secs) {
			return transactions;
		}

		/*
		 * Create the details of our new transaction and record them in our pending transaction list.
		 */
		struct transaction *t = cache_head;
		if (t) {
			cache_head = t->next;
		} else {
			t = malloc(sizeof(struct transaction));
			if (!t) {
				fprintf(stderr, "Out of memory!\n");
				exit(-1);
			}
		}

		t->size = (1024 * 1024) / 2100;
		t->fee = 0.00001;
		t->time = next_transaction_secs;

		if (!pending_head) {
			pending_head = t;
		} else {
			pending_tail->next = t;
		}

		t->next = NULL;
		t->prev = pending_tail;
		pending_tail = t;

		transactions++;

		/*
		 * Work out when the next transaction arrival is.
		 */
		double transaction_arrival = sim_pp(tps);
		next_transaction_secs += transaction_arrival;
	}
}

/*
 * create_block()
 *	Walk the list of pending transactions and simulate a block.
 */
static int create_block(double block_found_time)
{
	struct transaction *t = pending_head;
	if (!t) {
		return 0;
	}

	/*
	 * This isn't actually correct but it's a good approximation :-)
	 */
	int blocks = 0;
	int block_space = 1024 * 1024;
	while (block_space >= t->size) {
		block_space -= t->size;
		blocks++;

		/*
		 * Work out how old this block is.
		 */
		double age = block_found_time - t->time;
		double log_age = log10(age);
		double log_age_bucket = (double)NUM_BUCKETS_PER_ORDER * log_age;
		long int b = (long int)ceil(log_age_bucket);
		b += (NEGATIVE_ORDERS * NUM_BUCKETS_PER_ORDER);
		if (b < 0) {
			b = 0;
		}
		buckets[b]++;

		if (largest_bucket < b) {
			largest_bucket = b;
		}

		if (smallest_bucket > b) {
			smallest_bucket = b;
		}

		num_results++;

		struct transaction *next = t->next;

		/*
		 * Insert our old block on the re-use cache.
		 */
		t->next = cache_head;
		cache_head = t;

		pending_head = next;

		if (next) {
			next->prev = NULL;
		} else {
			pending_tail = NULL;
			break;
		}

		t = next;
	}

	return blocks;
}

/*
 * mine()
 *	Simulate a set of blocks being mined.
 */
static void mine(double tps, int num_blocks, FILE *f, double *cumulative_time, int *transactions_handled)
{
	int cumulative_transactions = 0;
	int cumulative_transactions_handled = 0;
	for (int i = 0; i < num_blocks; i++) {
		/*
		 * Randomize!
		 */
		unsigned int seed;
		if (fread(&seed, sizeof(seed), 1, f) != 1) {
			fclose(f);
			exit(-2);
		}

		srand(seed);

		/*
		 * Find the next block.
		 */
		double block_duration = sim_pp(1.0 / 600.0);

		/*
		 * What is the time at which this block is found?
		 */
		*cumulative_time += block_duration;

		/*
		 * Find the transactions that will arrive in that new block.
		 */
		int t = sim_transactions(*cumulative_time, tps);
		cumulative_transactions += t;

		int transactions_handled = create_block(*cumulative_time);
		cumulative_transactions_handled += transactions_handled;
		cumulative_transactions -= transactions_handled;
	}

	*transactions_handled = cumulative_transactions_handled;
}

/*
 * output_results()
 *	Generate the output results.
 */
static void output_results(void)
{
	double num_res = (double)num_results;

	double cumulative_ratio = 0.0;
	for (int i = smallest_bucket; i <= largest_bucket; i++) {
		double r = (double)buckets[i];
		cumulative_ratio += r;
		printf("%d | %.6f | %.6f | %.6f\n",
		       i, pow(10.0, (double)(i - (NEGATIVE_ORDERS * NUM_BUCKETS_PER_ORDER)) / (double)NUM_BUCKETS_PER_ORDER),
		       r / num_res, cumulative_ratio / num_res);
	}
}

/*
 * sim()
 *	Simulate mining.
 */
void sim(double tps, int num_blocks, int num_sims)
{
	/*
	 * We want some real randomness in our results.  Go and open a can of it!
	 */
	FILE *f = fopen("/dev/urandom", "rb");
	if (!f) {
		fprintf(stderr, "Failed to open /dev/urandom\n");
		return;
	}

	int divisor = num_sims / 100;
	if (divisor == 0) {
		divisor = 1;
	}

	/*
	 * Simulate many runs.
	 */
	for (int j = 0; j < num_sims; j++) {
		double cumulative_time = 0.0;
		int transactions_handled;
		mine(tps, num_blocks, f, &cumulative_time, &transactions_handled);

		if ((j % divisor) == 0) {
			fprintf(stderr, "Sim: %d completed\n", j);
		}

		/*
		 * Clean up the last simulation.
		 */
		struct transaction *t = pending_head;
		while (t) {
			struct transaction *next = t->next;
			free(t);
			t = next;
		}

		pending_head = NULL;
		pending_tail = NULL;
		pending_transactions = 0;
		next_transaction_secs = 0.0;
	}

	fclose(f);

	/*
	 * Produce output data.
	 */
	output_results();
}

int main(int argc, char **argv)
{
	if (argc != 4) {
		printf("usage: %s <starting-rate> <num-blocks> <num-sims>\n", argv[0]);
		exit(-1);
	}

	/*
	 * The TPS rate is expressed in the range 0 to 3.5.  3.5 represents a nominal arrival
	 * of 100% of the network's capacity, distributed with a Poisson distribution.  Doing
	 * this means that we don't actually worry about the size of the transactions or the
	 * number of them, just their relative capacity.
	 */
	double tps = atof(argv[1]);

	/*
	 * Number of blocks that we wish to model per simulation run.  If, say, this is 1008
	 * then this corresponds to a nominal week of mining as we're not modelling the
	 * network capacity expanding or contracting.
	 */
	int nb = atoi(argv[2]);

	/*
	 * Number of simulation runs.  Larger is better here.  100k simulations should give
	 * pretty consistent results; 1M is better :-)
	 */
	int ns = atoi(argv[3]);

	printf("initial TPS: %f, num blocks: %d, num simulations: %d\n-\n", tps, nb, ns);

	sim(tps, nb, ns);
}

