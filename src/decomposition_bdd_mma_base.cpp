#include "decomposition_bdd_mma_base.h"

namespace LPMP {

    ///////////////////////////////
    // Lagrange_multiplier_queue //
    ///////////////////////////////

    //////////////////
    // bdd_sub_base //
    //////////////////

    ////////////////////////////
    // decomposition_bdd_base //
    ////////////////////////////

    decomposition_bdd_base::decomposition_bdd_base(bdd_storage& bdd_storage_, decomposition_mma_options opt)
        : intervals(bdd_storage_.compute_intervals(opt.nr_threads)),
        intra_interval_message_passing_weight(opt.parallel_message_passing_weight)
    {
        intra_interval_message_passing_weight = 0.5;
        //std::cout << "decomposing BDDs into " << intervals.nr_intervals() << " intervals: ";
        //for(size_t i=0; i<intervals.nr_intervals(); ++i)
        //    std::cout << "[" << intervals.interval_boundaries[i] << "," << intervals.interval_boundaries[i+1] << "), ";
        //std::cout << "\n";

        assert(intra_interval_message_passing_weight >= 0 && intra_interval_message_passing_weight <= 1.0);
        const size_t nr_intervals = opt.nr_threads;
        auto [bdd_storages, duplicated_bdd_variables] = bdd_storage_.split_bdd_nodes(nr_intervals);

        //std::cout << "Allocate " << nr_intervals << " bdd bases\n";
        bdd_bases = std::make_unique<bdd_sub_base[]>(nr_intervals);
        std::vector<std::vector<typename bdd_storage::bdd_endpoints_>> bdd_endpoints(nr_intervals);

        bdd_endpoints.resize(nr_intervals);
#pragma omp parallel for
        for(size_t i=0; i<nr_intervals; ++i)
        {
            bdd_bases[i].base.init(bdd_storages[i]); 
            //std::cout << "BDD base " << i << " has " << bdd_bases[i].base.nr_variables() << " variables\n";
            bdd_endpoints[i] = bdd_storages[i].bdd_endpoints();
        }

        for(const auto [interval_1, bdd_nr_1, interval_2, bdd_nr_2] : duplicated_bdd_variables)

        {
            assert(interval_1 < interval_2);
            //const auto [first_bdd_var_1, first_bdd_index_1, last_bdd_var_1, last_bdd_index_1] = bdd_endpoints(interval_1, bdd_nr_1);
            //const auto [first_bdd_var_2, first_bdd_index_2, last_bdd_var_2, last_bdd_index_2] = bdd_endpoints(interval_2, bdd_nr_2);
            const auto [first_bdd_var_1, first_bdd_index_1, last_bdd_var_1, last_bdd_index_1] = bdd_endpoints[interval_1][bdd_nr_1];
            const auto [first_bdd_var_2, first_bdd_index_2, last_bdd_var_2, last_bdd_index_2] = bdd_endpoints[interval_2][bdd_nr_2];

            //std::cout << first_bdd_var_1 << "," <<  first_bdd_index_1 << "," <<  last_bdd_var_1 << "," <<  last_bdd_index_1 << "\n";
            //std::cout << first_bdd_var_2 << "," <<  first_bdd_index_2 << "," <<  last_bdd_var_2 << "," <<  last_bdd_index_2 << "\n";
            //std::cout << "\n";

            assert(last_bdd_var_1 == first_bdd_var_2);

            const auto offsets_1 = bdd_bases[interval_1].base.bdd_branch_node_offset(last_bdd_var_1, last_bdd_index_1);
            const auto offsets_2 = bdd_bases[interval_2].base.bdd_branch_node_offset(first_bdd_var_2, first_bdd_index_2);
            const endpoint e1 = {offsets_1[0], offsets_1[1], interval_2, offsets_2[0]};
            bdd_bases[interval_1].forward_endpoints.push_back(e1);
            const endpoint e2 = {offsets_2[0], offsets_2[1], interval_1, offsets_1[0]};
            bdd_bases[interval_2].backward_endpoints.push_back(e2);
        }

        for(size_t i=0; i<intervals.nr_intervals(); ++i)
        {
            auto endpoint_order = [](const auto& e1, const auto& e2) { return e1.opposite_interval_nr < e2.opposite_interval_nr; };
            std::sort(bdd_bases[i].forward_endpoints.begin(), bdd_bases[i].forward_endpoints.end(), endpoint_order);
            std::sort(bdd_bases[i].backward_endpoints.begin(), bdd_bases[i].backward_endpoints.end(), endpoint_order);
        }

        costs.clear();
        costs.resize(bdd_storage_.nr_variables(), 0.0);
    }

    size_t decomposition_bdd_base::nr_variables() const
    {
        return bdd_bases[0].base.nr_variables();
    }

    void decomposition_bdd_base::set_cost(const double c, const size_t var)
    {
        assert(var < costs.size());
        assert(intervals.interval(var) < intervals.nr_intervals());
        assert(costs[var] == 0.0);
        costs[var] = c;
        size_t nr_cover_intervals = 0;
        for(size_t i=0; i<intervals.nr_intervals(); ++i)
            if(bdd_bases[i].base.nr_bdds(var) > 0)
                ++nr_cover_intervals;

        for(size_t i=0; i<intervals.nr_intervals(); ++i)
        {
            if(bdd_bases[i].base.nr_bdds(var) > 0)
            {
                const double interval_cost = c/double(nr_cover_intervals);
                bdd_bases[i].base.set_cost(interval_cost, var);
            }
        }
    }

    void decomposition_bdd_base::backward_run()
    {
        // TODO: parallelize
 
        for(std::ptrdiff_t interval_nr=intervals.nr_intervals()-1; interval_nr>=0; --interval_nr)
        {
            bdd_bases[interval_nr].read_in_Lagrange_multipliers(bdd_bases[interval_nr].backward_queue);
            bdd_bases[interval_nr].read_in_Lagrange_multipliers(bdd_bases[interval_nr].forward_queue);
            bdd_bases[interval_nr].base.backward_run();
            bdd_bases[interval_nr].base.compute_lower_bound();
        }
    }

    void decomposition_bdd_base::solve(const size_t max_iter, const double tolerance)
    {
        std::cout << "initial lower bound = " << lower_bound() << "\n";
        
        /*
        for(size_t i=0; i<max_iter; ++i)
        {
            for(size_t t=0; t<intervals.nr_intervals(); ++t)
            {
                this->min_marginal_averaging_forward(t);
                this->min_marginal_averaging_backward(t);
            }
        }
        */
        for(size_t i=0; i<max_iter; ++i)
        {
#pragma omp parallel for
            for(size_t t=0; t<intervals.nr_intervals(); ++t)
            {
                min_marginal_averaging_forward(t);
                /*
                if(i % 2 == 0 && t % 2 == 0)
                    min_marginal_averaging_forward(t);
                else if(i % 2 == 0 && t % 2 == 1)
                    min_marginal_averaging_backward(t);
                else if(i % 2 == 1 && t % 2 == 0)
                    min_marginal_averaging_backward(t);
                else
                { 
                    assert(i % 2 == 1 && t % 2 == 1);
                    min_marginal_averaging_forward(t);
                }
                */
            }
#pragma omp parallel for
            for(size_t t=0; t<intervals.nr_intervals(); ++t)
            {
                min_marginal_averaging_backward(t);
                /*
                if(i % 2 == 0 && t % 2 == 0)
                    min_marginal_averaging_backward(t);
                else if(i % 2 == 0 && t % 2 == 1)
                    min_marginal_averaging_forward(t);
                else if(i % 2 == 1 && t % 2 == 0)
                    min_marginal_averaging_forward(t);
                else
                { 
                    assert(i % 2 == 1 && t % 2 == 1);
                    min_marginal_averaging_backward(t);
                }
                */
            }
#pragma omp parallel for
            for(size_t t=0; t<intervals.nr_intervals(); ++t)
            {
                bdd_bases[t].base.compute_lower_bound();
            }
            //if(t == 0)
            std::cout << "iteration " << i << ", lower bound = " << lower_bound() << "\n";
        }

        backward_run(); // To flush out the Lagrange multiplier queues and recompute final lower bound
        std::cout << "final lower bound = " << lower_bound() << "\n"; 
    }

    void decomposition_bdd_base::bdd_sub_base::read_in_Lagrange_multipliers(typename decomposition_bdd_base::Lagrange_multiplier_queue& q)
    {
        //std::cout << "read in queue of size " << q.queue.size() << "\n";
        std::lock_guard<std::mutex> lck(q.mutex);
        std::vector<double> deltas;
        while(!q.queue.empty())
        {
            assert(q.queue.size() >= 4); // at least two deltas are present
            const size_t first_node = q.queue.front().first_node;
            q.queue.pop();
            const size_t nr_deltas = q.queue.front().nr_deltas;
            assert(nr_deltas % 2 == 0);
            q.queue.pop();
            assert(q.queue.size() >= nr_deltas);
            deltas.clear();
            //std::cout << "Lagrange multiplier update: ";
            for(size_t c=0; c<nr_deltas; ++c)
            {
                deltas.push_back(q.queue.front().delta);
                //std::cout << deltas.back() << ", ";
                assert(deltas.back() >= 0.0);
                q.queue.pop();
            }
            //std::cout << "\n";

            base.update_arc_costs(first_node, deltas.begin(), deltas.end()); 
        }
    }

    template<typename ITERATOR>
        void decomposition_bdd_base::Lagrange_multiplier_queue::write_to_queue(const size_t node_nr, ITERATOR begin, ITERATOR end)
        {
            //std::cout << "write Lagrange multipliers: ";
            Lagrange_multiplier L;
            L.first_node = node_nr;
            queue.push(L);
            //std::cout << node_nr << ", ";
            L.nr_deltas = std::distance(begin, end);
            queue.push(L);
            //std::cout << L.nr_deltas << ", ";
            for(auto it=begin; it!=end; ++it)
            {
                L.delta = *it;
                queue.push(L);
                //std::cout << *it << ", ";
            }
            //std::cout << "\n";
        }

    // after forward pass, collect min marginals of all duplicated arcs are computed and sent
    void decomposition_bdd_base::min_marginal_averaging_forward(const size_t interval_nr)
    {
        assert(interval_nr < intervals.nr_intervals());
        if(interval_nr == 0)
            assert(bdd_bases[interval_nr].forward_queue.queue.empty());
        bdd_bases[interval_nr].read_in_Lagrange_multipliers(bdd_bases[interval_nr].forward_queue);

        bdd_bases[interval_nr].base.min_marginal_averaging_forward();

        // TODO: use stack variables
        std::vector<double> arc_marginals;

        size_t last_interval_nr = std::numeric_limits<size_t>::max();
        for(const auto& e : bdd_bases[interval_nr].forward_endpoints)
        {
            assert(e.opposite_interval_nr != interval_nr);
            if(last_interval_nr != e.opposite_interval_nr)
            {
                if(last_interval_nr != std::numeric_limits<size_t>::max())
                    bdd_bases[last_interval_nr].forward_queue.mutex.unlock();
                bdd_bases[e.opposite_interval_nr].forward_queue.mutex.lock();
                last_interval_nr = e.opposite_interval_nr;
            }
            bdd_bases[interval_nr].base.get_arc_marginals(e.first_node, e.last_node, arc_marginals);
            const double min_arc_cost = *std::min_element(arc_marginals.begin(), arc_marginals.end());
            for(auto& x : arc_marginals)
            {
                x -= min_arc_cost;
                x *= -intra_interval_message_passing_weight;
                assert(x <= 0.0);
            }
            assert(*std::max_element(arc_marginals.begin(), arc_marginals.end()) == 0.0);
            bdd_bases[interval_nr].base.update_arc_costs(e.first_node, arc_marginals.begin(), arc_marginals.end());

            for(auto& x : arc_marginals)
                x *= -1.0;

            bdd_bases[e.opposite_interval_nr].forward_queue.write_to_queue(e.first_node_opposite_interval, arc_marginals.begin(), arc_marginals.end());
        }
        if(last_interval_nr != std::numeric_limits<size_t>::max())
            bdd_bases[last_interval_nr].forward_queue.mutex.unlock();
    }

    void decomposition_bdd_base::min_marginal_averaging_backward(const size_t interval_nr)
    {
        assert(interval_nr < intervals.nr_intervals());
        if(interval_nr == intervals.nr_intervals()-1)
            assert(bdd_bases[interval_nr].backward_queue.queue.empty());
        bdd_bases[interval_nr].read_in_Lagrange_multipliers(bdd_bases[interval_nr].backward_queue);

        bdd_bases[interval_nr].base.min_marginal_averaging_backward();

        std::vector<double> arc_marginals; // TODO: use stack variables

        size_t last_interval_nr = std::numeric_limits<size_t>::max();
        for(const auto& e : bdd_bases[interval_nr].backward_endpoints)
        {
            assert(e.opposite_interval_nr != interval_nr);
            if(last_interval_nr != e.opposite_interval_nr)
            {
                if(last_interval_nr != std::numeric_limits<size_t>::max())
                    bdd_bases[last_interval_nr].backward_queue.mutex.unlock();
                bdd_bases[e.opposite_interval_nr].backward_queue.mutex.lock();
                last_interval_nr = e.opposite_interval_nr;
            }

            bdd_bases[interval_nr].base.get_arc_marginals(e.first_node, e.last_node, arc_marginals);
            const double min_arc_cost = *std::min_element(arc_marginals.begin(), arc_marginals.end());
            for(auto& x : arc_marginals)
            {
                x -= min_arc_cost;
                x *= -intra_interval_message_passing_weight;
                assert(x <= 0.0);
            }
            assert(*std::max_element(arc_marginals.begin(), arc_marginals.end()) == 0.0);
            bdd_bases[interval_nr].base.update_arc_costs(e.first_node, arc_marginals.begin(), arc_marginals.end());

            for(auto& x : arc_marginals)
                x *= -1.0;

            bdd_bases[e.opposite_interval_nr].backward_queue.write_to_queue(e.first_node_opposite_interval, arc_marginals.begin(), arc_marginals.end());
        }
        if(last_interval_nr != std::numeric_limits<size_t>::max())
            bdd_bases[last_interval_nr].backward_queue.mutex.unlock();
    }

    double decomposition_bdd_base::lower_bound()
    {
        double lb = 0.0;
        for(size_t i=0; i<intervals.nr_intervals(); ++i)
            lb += bdd_bases[i].base.lower_bound();
        return lb;
    }

    std::vector<double> decomposition_bdd_base::total_min_marginals()
    {
        for(size_t i=0; i<intervals.nr_intervals(); ++i)
        {
            bdd_bases[i].read_in_Lagrange_multipliers(bdd_bases[i].forward_queue);
            bdd_bases[i].read_in_Lagrange_multipliers(bdd_bases[i].backward_queue);
        }

        std::vector<double> total_min_marginals(nr_variables(), 0.0);
//#pragma omp parallel for
        for(size_t i=0; i<intervals.nr_intervals(); ++i)
        {
            const auto intn_total_min_margs = bdd_bases[i].base.total_min_marginals();
            assert(intn_total_min_margs.size() == total_min_marginals.size());
//#pragma barrier
            for(size_t i=0; i<intn_total_min_margs.size(); ++i)
                total_min_marginals[i] += intn_total_min_margs[i];

        }
        return total_min_marginals;
    }

}
