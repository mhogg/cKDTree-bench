#include <Python.h>
#include "numpy/arrayobject.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <vector>
#include <string>
#include <sstream>
#include <new>
#include <typeinfo>
#include <stdexcept>
#include <ios>

#define CKDTREE_METHODS_IMPL
#include "ckdtree_decl.h"
#include "ckdtree_methods.h"
#include "cpp_exc.h"
#include "rectangle.h"


static void
traverse_no_checking(const ckdtree *self, const ckdtree *other,
                     std::vector<npy_intp> **results, 
                     const ckdtreenode *node1, const ckdtreenode *node2)
{
    const ckdtreenode *lnode1;
    const ckdtreenode *lnode2;
    const npy_intp *self_indices = self->raw_indices;
    const npy_intp *other_indices = other->raw_indices;    
    std::vector<npy_intp> *results_i;
    npy_intp i, j;
    
    if (node1->split_dim == -1) {   /* leaf node */
        lnode1 = node1;
        
        const npy_intp start1 = lnode1->start_idx;
        const npy_intp start2 = lnode2->start_idx;
        const npy_intp end1 = lnode1->end_idx;
        const npy_intp end2 = lnode2->end_idx;
        
        if (node2->split_dim == -1) {  /* leaf node */
            lnode2 = node2;
            
            for (i = start1; i < end1; ++i) {
                results_i = results[self_indices[i]];
                for (j = start2; j < end2; ++j)
                    results_i->push_back(other_indices[j]);
            }
        }
        else {            
            traverse_no_checking(self, other, results, node1, node2->less);   
            traverse_no_checking(self, other, results, node1, node2->greater);
        }
    }
    else {        
        traverse_no_checking(self, other, results, node1->less, node2);
        traverse_no_checking(self, other, results, node1->greater, node2);
    }
}


static void
traverse_checking(const ckdtree *self, const ckdtree *other,
                  std::vector<npy_intp> **results,
                  const ckdtreenode *node1, const ckdtreenode *node2,
                  RectRectDistanceTracker *tracker)
{
    const ckdtreenode *lnode1;
    const ckdtreenode *lnode2;
    std::vector<npy_intp> *results_i;
    npy_float64 d;
    npy_intp i, j;

    if (tracker->min_distance > tracker->upper_bound * tracker->epsfac)
        return;
    else if (tracker->max_distance < tracker->upper_bound / tracker->epsfac)
        traverse_no_checking(self, other, results, node1, node2);
    else if (node1->split_dim == -1) { /* 1 is leaf node */
        lnode1 = node1;
        
        if (node2->split_dim == -1) {  /* 1 & 2 are leaves */

            /* brute-force */
            const npy_float64 p = tracker->p;
            const npy_float64 tub = tracker->upper_bound;
            const npy_float64 tmd = tracker->max_distance;
            const npy_float64 *self_data = self->raw_data;
            const npy_intp *self_indices = self->raw_indices;
            const npy_float64 *other_data = other->raw_data;
            const npy_intp *other_indices = other->raw_indices;
            const npy_intp m = self->m;
            
            lnode2 = node2;
            
            const npy_intp start1 = lnode1->start_idx;
            const npy_intp start2 = lnode2->start_idx;
            const npy_intp end1 = lnode1->end_idx;
            const npy_intp end2 = lnode2->end_idx;
        
            prefetch_datapoint(self_data 
                + self_indices[start1]*m, m);
            
            if (start1 < end1)
                prefetch_datapoint(self_data
                  + self_indices[start1+1]*m, m);                                    
            
            for (i = start1; i < end1; ++i) {
            
                if (i < end1-2)
                    prefetch_datapoint(self_data + self_indices[i+2]*m, m);
                                  
                prefetch_datapoint(other_data 
                    + other_indices[start2]*m, m);
                    
                if (start2 < end2)
                    prefetch_datapoint(other_data 
                        + other_indices[start2+1]*m, m);
                        
                results_i = results[self_indices[i]];        
                                                                
                for (j = start2; j < end2; ++j) {
                
                    if (j < end2-2)
                        prefetch_datapoint(other_data 
                            + other_indices[j+2]*m, m);
                
                    d = _distance_p(
                            self_data + self_indices[i] * m,
                            other_data + other_indices[j] * m,
                            p, m, tmd);
        
                    if (d <= tub)
                        results_i->push_back(other->raw_indices[j]);
                }
            }
                       
        }                
        else { /* 1 is a leaf node, 2 is inner node */

            tracker->push_less_of(2, node2);
            traverse_checking(
                self, other, results, node1, node2->less, tracker);
            tracker->pop();
                
            tracker->push_greater_of(2, node2);            
            traverse_checking(
                self, other, results, node1, node2->greater, tracker);
            tracker->pop();
        }
    }        
    else {  /* 1 is an inner node */
        if (node2->split_dim == -1) { /* 1 is an inner node, 2 is a leaf node */
            tracker->push_less_of(1, node1);
            traverse_checking(
                self, other, results, node1->less, node2, tracker);
            tracker->pop();
                
            tracker->push_greater_of(1, node1);
            traverse_checking(
                self, other, results, node1->greater, node2, tracker);
            tracker->pop();
        }    
        else { /* 1 & 2 are inner nodes */
            
            tracker->push_less_of(1, node1);
            tracker->push_less_of(2, node2);
            traverse_checking(
                self, other, results, node1->less, node2->less, tracker);
            tracker->pop();
                
            tracker->push_greater_of(2, node2);
            traverse_checking(
                self, other, results, node1->less, node2->greater, tracker);
            tracker->pop();
            tracker->pop();

            
            tracker->push_greater_of(1, node1);
            tracker->push_less_of(2, node2);
            traverse_checking(
                self, other, results, node1->greater, node2->less, tracker);
            tracker->pop();
                
            tracker->push_greater_of(2, node2);
            traverse_checking(
                self, other, results, node1->greater, node2->greater, tracker);
            tracker->pop();
            tracker->pop();
        }
    }
}
    
    
extern "C" PyObject*
query_ball_tree(const ckdtree *self, const ckdtree *other, 
                const npy_float64 r, const npy_float64 p, const npy_float64 eps,
                std::vector<npy_intp> **results)
{

    /* release the GIL */
    NPY_BEGIN_ALLOW_THREADS   
    {
        try {
            Rectangle r1(self->m, self->raw_mins, self->raw_maxes);
            Rectangle r2(other->m, other->raw_mins, other->raw_maxes);
            
            RectRectDistanceTracker tracker(r1, r2, p, eps, r);
            
            traverse_checking(self, other, results, self->ctree, other->ctree, 
                &tracker);
             
        } 
        catch(...) {
            translate_cpp_exception_with_gil();
        }
    }  
    /* reacquire the GIL */
    NPY_END_ALLOW_THREADS

    if (PyErr_Occurred()) 
        /* true if a C++ exception was translated */
        return NULL;
    else {
        /* return None if there were no errors */
        Py_RETURN_NONE;
    }
}        
