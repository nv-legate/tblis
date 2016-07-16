#ifndef _TBLIS_TENSOR_CLASS_HPP_
#define _TBLIS_TENSOR_CLASS_HPP_

#include "tblis.hpp"

namespace tblis
{

namespace detail
{
    struct sort_by_idx_helper
    {
        const std::string& idx;

        sort_by_idx_helper(const std::string& idx) : idx(idx) {}

        bool operator()(unsigned i, unsigned j) const
        {
            return idx[i] < idx[j];
        }
    };

    inline sort_by_idx_helper sort_by_idx(const std::string& idx)
    {
        return sort_by_idx_helper(idx);
    }

    struct sort_by_stride_helper
    {
        std::vector<const std::vector<stride_type>*> strides;

        sort_by_stride_helper(const std::vector<const std::vector<stride_type>*>& strides)
        : strides(strides) {}

        sort_by_stride_helper(std::vector<const std::vector<stride_type>*>&& strides)
        : strides(std::move(strides)) {}

        sort_by_stride_helper(const std::vector<std::vector<stride_type>*>& strides)
        : strides(strides.begin(), strides.end()) {}

        bool operator()(unsigned i, unsigned j) const
        {
            stride_type min_i = (*strides[0])[i];
            stride_type min_j = (*strides[0])[j];

            for (size_t k = 1;k < strides.size();k++)
            {
                min_i = std::min(min_i, (*strides[k])[i]);
                min_j = std::min(min_j, (*strides[k])[j]);
            }

            return min_i < min_j;
        }
    };

    inline sort_by_stride_helper sort_by_stride(const std::vector<const std::vector<stride_type>*>& strides)
    {
        return {strides};
    }

    inline sort_by_stride_helper sort_by_stride(std::vector<const std::vector<stride_type>*>&& strides)
    {
        return {std::move(strides)};
    }

    inline sort_by_stride_helper sort_by_stride(const std::vector<std::vector<stride_type>*>& strides)
    {
        return {strides};
    }

    template <typename... Strides>
    sort_by_stride_helper sort_by_stride(const Strides&... strides)
    {
        return {{&strides...}};
    }

    template <typename T>
    bool are_congruent_along(const const_tensor_view<T>& A,
                             const const_tensor_view<T>& B, unsigned dim)
    {
        if (A.dimension() < B.dimension()) swap(A, B);

        unsigned ndim = A.dimension();
        auto sA = A.strides().begin();
        auto sB = B.strides().begin();
        auto lA = A.lengths().begin();
        auto lB = B.lengths().begin();

        if (B.dimension() == ndim)
        {
            if (!std::equal(sA, sA+ndim, sB)) return false;
            if (!std::equal(lA, lA+dim, lB)) return false;
            if (!std::equal(lA+dim+1, lA+ndim, lB+dim+1)) return false;
        }
        else if (B.dimension() == ndim-1)
        {
            if (!std::equal(sA, sA+dim, sB)) return false;
            if (!std::equal(sA+dim+1, sA+ndim, sB+dim)) return false;
            if (!std::equal(lA, lA+dim, lB)) return false;
            if (!std::equal(lA+dim+1, lA+ndim, lB+dim)) return false;
        }
        else
        {
            return false;
        }

        return true;
    }

    inline bool are_compatible(const std::vector<idx_type>& len_A,
                               const std::vector<stride_type>& stride_A,
                               const std::vector<idx_type>& len_B,
                               const std::vector<stride_type>& stride_B)
    {
        assert(len_A.size() == stride_A.size());
        std::vector<size_t> dims_A = range(len_A.size());
        stl_ext::sort(dims_A, detail::sort_by_stride(stride_A));
        auto len_Ar = stl_ext::permuted(len_A, dims_A);
        auto stride_Ar = stl_ext::permuted(stride_A, dims_A);

        assert(len_B.size() == stride_B.size());
        std::vector<size_t> dims_B = range(len_B.size());
        stl_ext::sort(dims_B, detail::sort_by_stride(stride_B));
        auto len_Br = stl_ext::permuted(len_B, dims_B);
        auto stride_Br = stl_ext::permuted(stride_B, dims_B);

        if (stl_ext::prod(len_Ar) != stl_ext::prod(len_Br))
            return false;

        MArray::viterator<> it_A(len_Ar, stride_Ar);
        MArray::viterator<> it_B(len_Br, stride_Br);

        stride_type off_A = 0, off_B = 0;
        while (it_A.next(off_A) + it_B.next(off_B))
            if (off_A != off_B) return false;

        return true;
    }

    template <typename T>
    bool are_compatible(const const_tensor_view<T>& A,
                        const const_tensor_view<T>& B)
    {
        return A.data() == B.data() &&
            are_compatible(A.lengths(), A.strides(),
                           B.lengths(), B.strides());
    }

    template <typename T>
    int check_tensor_indices(const const_tensor_view<T>& A, const std::string& idx_A)
    {
        using stl_ext::sort;

        std::vector<std::pair<char,idx_type> > idx_len;
        idx_len.reserve(A.dimension());

        assert(idx_A.size() == A.dimension());

        for (unsigned i = 0;i < A.dimension();i++)
        {
            idx_len.emplace_back(idx_A[i], A.length(i));
        }

        sort(idx_len);

        for (unsigned i = 1;i < idx_len.size();i++)
        {
            if (idx_len[i].first  == idx_len[i-1].first)
                assert(idx_len[i].second == idx_len[i-1].second);
        }

        return 0;
    }

    template <typename T>
    int check_tensor_indices(const tensor_view<T>& A, const std::string& idx_A)
    {
        return check_tensor_indices(reinterpret_cast<const const_tensor_view<T>&>(A), idx_A);
    }

    template <typename T>
    int check_tensor_indices(const const_tensor_view<T>& A, std::string idx_A,
                             const const_tensor_view<T>& B, std::string idx_B,
                             bool has_A_only, bool has_B_only, bool has_AB)
    {
        using stl_ext::sort;
        using stl_ext::unique;
        using stl_ext::intersection;
        using stl_ext::exclusion;

        std::vector<std::pair<char,idx_type>> idx_len;
        idx_len.reserve(A.dimension()+
                        B.dimension());

        assert(idx_A.size() == A.dimension());
        assert(idx_B.size() == B.dimension());

        for (unsigned i = 0;i < A.dimension();i++)
        {
            idx_len.emplace_back(idx_A[i], A.length(i));
        }

        for (unsigned i = 0;i < B.dimension();i++)
        {
            idx_len.emplace_back(idx_B[i], B.length(i));
        }

        sort(idx_len);

        for (unsigned i = 1;i < idx_len.size();i++)
        {
            if (idx_len[i].first  == idx_len[i-1].first)
                assert(idx_len[i].second == idx_len[i-1].second);
        }

        unique(idx_A);
        unique(idx_B);

        auto idx_AB = intersection(idx_A, idx_B);
        auto idx_A_only = exclusion(idx_A, idx_B);
        auto idx_B_only = exclusion(idx_B, idx_A);

        assert(idx_A_only.empty() || has_A_only);
        assert(idx_B_only.empty() || has_B_only);
        assert(idx_AB.empty()     || has_AB);

        return 0;
    }

    template <typename T>
    int check_tensor_indices(const const_tensor_view<T>& A, std::string idx_A,
                             const       tensor_view<T>& B, std::string idx_B,
                             bool has_A_only, bool has_B_only, bool has_AB)
    {
        return check_tensor_indices(A, idx_A,
                                    reinterpret_cast<const const_tensor_view<T>&>(B), idx_B,
                                    has_A_only, has_B_only, has_AB);
    }

    template <typename T>
    int check_tensor_indices(const const_tensor_view<T>& A, std::string idx_A,
                             const const_tensor_view<T>& B, std::string idx_B,
                             const       tensor_view<T>& C, std::string idx_C,
                             bool has_A_only, bool has_B_only, bool has_C_only,
                             bool has_AB, bool has_AC, bool has_BC,
                             bool has_ABC)
    {
        using stl_ext::sort;
        using stl_ext::unique;
        using stl_ext::intersection;
        using stl_ext::exclusion;

        std::vector<std::pair<char,idx_type>> idx_len;
        idx_len.reserve(A.dimension()+
                        B.dimension()+
                        C.dimension());

        assert(idx_A.size() == A.dimension());
        assert(idx_B.size() == B.dimension());
        assert(idx_C.size() == C.dimension());

        for (unsigned i = 0;i < A.dimension();i++)
        {
            idx_len.emplace_back(idx_A[i], A.length(i));
        }

        for (unsigned i = 0;i < B.dimension();i++)
        {
            idx_len.emplace_back(idx_B[i], B.length(i));
        }

        for (unsigned i = 0;i < C.dimension();i++)
        {
            idx_len.emplace_back(idx_C[i], C.length(i));
        }

        sort(idx_len);

        for (unsigned i = 1;i < idx_len.size();i++)
        {
            if (idx_len[i].first  == idx_len[i-1].first)
                assert(idx_len[i].second == idx_len[i-1].second);
        }

        unique(idx_A);
        unique(idx_B);
        unique(idx_C);

        auto idx_ABC = intersection(idx_A, idx_B, idx_C);
        auto idx_AB = exclusion(intersection(idx_A, idx_B), idx_C);
        auto idx_AC = exclusion(intersection(idx_A, idx_C), idx_B);
        auto idx_BC = exclusion(intersection(idx_B, idx_C), idx_A);
        auto idx_A_only = exclusion(idx_A, idx_B, idx_C);
        auto idx_B_only = exclusion(idx_B, idx_A, idx_C);
        auto idx_C_only = exclusion(idx_C, idx_A, idx_B);

        assert(idx_A_only.empty() || has_A_only);
        assert(idx_B_only.empty() || has_B_only);
        assert(idx_C_only.empty() || has_C_only);
        assert(idx_AB.empty()     || has_AB);
        assert(idx_AC.empty()     || has_AC);
        assert(idx_BC.empty()     || has_BC);
        assert(idx_ABC.empty()    || has_ABC);

        return 0;
    }

    template <typename T, typename=void>
    struct pointer_type;

    template <typename T>
    struct pointer_type<T, stl_ext::enable_if_t<std::is_pointer<stl_ext::decay_t<T>>::value>>
    {
        typedef stl_ext::remove_pointer_t<stl_ext::decay_t<T>> type;
    };

    template <typename T>
    struct pointer_type<T, has_member<decltype(std::declval<T>().data())>>
    {
        typedef stl_ext::remove_pointer_t<decltype(std::declval<T>().data())> type;
    };

    template <typename T>
    using pointer_type_t = typename pointer_type<T>::type;

    template <typename... Args> struct check_template_types;

    template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
              typename U>
    struct check_template_types<T, A_ptr, A_len, A_stride, A_idx, U>
    {
        typedef stl_ext::enable_if<(std::is_same<float,T>::value ||
                                    std::is_same<double,T>::value ||
                                    std::is_same<scomplex,T>::value ||
                                    std::is_same<dcomplex,T>::value) &&
                                   std::is_same<pointer_type_t<A_ptr>,T>::value &&
                                   std::is_integral<pointer_type_t<A_len>>::value &&
                                   std::is_integral<pointer_type_t<A_stride>>::value &&
                                   std::is_integral<pointer_type_t<A_idx>>::value,
                                   U> type;
    };

    template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                          typename B_ptr, typename B_len, typename B_stride, typename B_idx,
              typename U>
    struct check_template_types<T, A_ptr, A_len, A_stride, A_idx,
                                   B_ptr, B_len, B_stride, B_idx, U>
    {
        typedef stl_ext::enable_if<(std::is_same<float,T>::value ||
                                    std::is_same<double,T>::value ||
                                    std::is_same<scomplex,T>::value ||
                                    std::is_same<dcomplex,T>::value) &&
                                   std::is_same<pointer_type_t<A_ptr>,T>::value &&
                                   std::is_same<pointer_type_t<B_ptr>,T>::value &&
                                   std::is_integral<pointer_type_t<A_len>>::value &&
                                   std::is_integral<pointer_type_t<B_len>>::value &&
                                   std::is_integral<pointer_type_t<A_stride>>::value &&
                                   std::is_integral<pointer_type_t<B_stride>>::value &&
                                   std::is_integral<pointer_type_t<A_idx>>::value &&
                                   std::is_integral<pointer_type_t<B_idx>>::value,
                                   U> type;
    };

    template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                          typename B_ptr, typename B_len, typename B_stride, typename B_idx,
                          typename C_ptr, typename C_len, typename C_stride, typename C_idx,
              typename U>
    struct check_template_types<T, A_ptr, A_len, A_stride, A_idx,
                                   B_ptr, B_len, B_stride, B_idx,
                                   C_ptr, C_len, C_stride, C_idx, U>
    {
        typedef stl_ext::enable_if<(std::is_same<float,T>::value ||
                                    std::is_same<double,T>::value ||
                                    std::is_same<scomplex,T>::value ||
                                    std::is_same<dcomplex,T>::value) &&
                                   std::is_same<pointer_type_t<A_ptr>,T>::value &&
                                   std::is_same<pointer_type_t<B_ptr>,T>::value &&
                                   std::is_same<pointer_type_t<C_ptr>,T>::value &&
                                   std::is_integral<pointer_type_t<A_len>>::value &&
                                   std::is_integral<pointer_type_t<B_len>>::value &&
                                   std::is_integral<pointer_type_t<C_len>>::value &&
                                   std::is_integral<pointer_type_t<A_stride>>::value &&
                                   std::is_integral<pointer_type_t<B_stride>>::value &&
                                   std::is_integral<pointer_type_t<C_stride>>::value &&
                                   std::is_integral<pointer_type_t<A_idx>>::value &&
                                   std::is_integral<pointer_type_t<B_idx>>::value &&
                                   std::is_integral<pointer_type_t<C_idx>>::value,
                                   U> type;
    };

    template <typename... Args>
    using check_template_types_t = typename check_template_types<Args...>::type;

    template <typename Len>
    stl_ext::enable_if_t<std::is_pointer<Len>::value,std::vector<idx_type>>
    make_len(unsigned ndim, const Len& x)
    {
        return {x, x+ndim};
    }

    template <typename Len>
    stl_ext::enable_if_t<!std::is_pointer<Len>::value,std::vector<idx_type>>
    make_len(unsigned ndim, const Len& x)
    {
        assert(x.size() == ndim);
        return {x.data(), x.data()+ndim};
    }

    template <typename Stride>
    stl_ext::enable_if_t<std::is_pointer<Stride>::value,std::vector<stride_type>>
    make_stride(unsigned ndim, const Stride& x)
    {
        return {x, x+ndim};
    }

    template <typename Stride>
    stl_ext::enable_if_t<!std::is_pointer<Stride>::value,std::vector<stride_type>>
    make_stride(unsigned ndim, const Stride& x)
    {
        assert(x.size() == ndim);
        return {x.data(), x.data()+ndim};
    }

    template <typename Idx>
    stl_ext::enable_if_t<std::is_pointer<Idx>::value,std::string>
    make_idx(unsigned ndim, const Idx& x)
    {
        return {x, x+ndim};
    }

    template <typename Idx>
    stl_ext::enable_if_t<!std::is_pointer<Idx>::value,std::string>
    make_idx(unsigned ndim, const Idx& x)
    {
        assert(x.size() == ndim);
        return {x.data(), x.data()+ndim};
    }

    template <typename Ptr>
    stl_ext::enable_if_t<std::is_pointer<Ptr>::value,pointer_type_t<Ptr>*>
    make_ptr(Ptr& x)
    {
        return x;
    }

    template <typename Ptr>
    stl_ext::enable_if_t<std::is_pointer<Ptr>::value,const pointer_type_t<Ptr>*>
    make_ptr(const Ptr& x)
    {
        return x;
    }

    template <typename Ptr>
    stl_ext::enable_if_t<!std::is_pointer<Ptr>::value,pointer_type_t<Ptr>*>
    make_ptr(Ptr& x)
    {
        return x.data();
    }

    template <typename Ptr>
    stl_ext::enable_if_t<!std::is_pointer<Ptr>::value,const pointer_type_t<Ptr>*>
    make_ptr(const Ptr& x)
    {
        return x.data();
    }
}

template <typename T>
void diagonal(const_tensor_view<T>& AD, std::string& idx_AD)
{
    assert(AD.dimension() == idx_AD.size());

    unsigned ndim_A = AD.dimension();
    std::vector<unsigned> inds_A = MArray::range(ndim_A);
    stl_ext::sort(inds_A, detail::sort_by_idx(idx_AD));

    std::string idx = idx_AD;
    std::vector<idx_type> len(ndim_A);
    std::vector<stride_type> stride(ndim_A);

    unsigned ndim_AD = 0;
    for (unsigned i = 0;i < ndim_A;i++)
    {
        if (AD.length(inds_A[i]) == 1)
        {
            //do nothing
        }
        else if (i == 0 || idx[inds_A[i]] != idx[inds_A[i-1]])
        {
            idx_AD[ndim_AD] = idx[inds_A[i]];
            len[ndim_AD] = AD.length(inds_A[i]);
            stride[ndim_AD] = AD.stride(inds_A[i]);
            ndim_AD++;
        }
        else
        {
            assert(AD.length(ndim_AD) == AD.length(inds_A[i]));
            stride[ndim_AD] += AD.stride(inds_A[i]);
        }
    }

    idx_AD.resize(ndim_AD);
    len.resize(ndim_AD);
    stride.resize(ndim_AD);

    AD.reset(len, AD.data(), stride);
}

template <typename T>
const_tensor_view<T> diagonal_of(const_tensor_view<T> A, std::string& idx_AD)
{
    diagonal(A, idx_AD);
    return A;
}

template <typename T>
void diagonal(tensor_view<T>& AD, std::string& idx_AD)
{
    diagonal(reinterpret_cast<const_tensor_view<T>&>(AD), idx_AD);
}

template <typename T>
tensor_view<T> diagonal_of(tensor_view<T> A, std::string& idx_AD)
{
    diagonal(A, idx_AD);
    return A;
}

template <typename T>
void partition(const_tensor_view<T> A,
               const_tensor_view<T>& A0, const_tensor_view<T>& A1,
               unsigned dim, idx_type off)
{
    assert(&A0 != &A1);
    assert(dim < A.dimension());
    assert(off >= 0);

    std::vector<idx_type> len = A.lengths();
    off = std::min(off, len[dim]);

    len[dim] -= off;
    A1.reset(len, A.data()+off*A.stride(dim), A.strides());

    len[dim] = off;
    A0.reset(len, A.data(), A.strides());
}

template <typename T>
void partition(tensor_view<T> A,
               tensor_view<T>& A0, tensor_view<T>& A1,
               unsigned dim, idx_type off)
{
    partition(A,
              reinterpret_cast<const_tensor_view<T>&>(A0),
              reinterpret_cast<const_tensor_view<T>&>(A1),
              dim, off);
}

template <typename T>
void unpartition(const_tensor_view<T> A0, const_tensor_view<T> A1,
                 const_tensor_view<T>& A,
                 unsigned dim)
{
    assert(dim < A0.dimension());
    assert(detail::are_congruent_along(A0, A1, dim));
    assert(A0.data()+A0.length(dim)*A0.stride(dim) == A1.data());

    std::vector<idx_type> len = A0.lengths();
    len[dim] += A1.length(dim);
    A.reset(len, A0.data(), A0.strides());
}

template <typename T>
void unpartition(tensor_view<T> A0, tensor_view<T> A1,
                 tensor_view<T>& A,
                 unsigned dim)
{
    unpartition(A0, A1,
                reinterpret_cast<const_tensor_view<T>&>(A),
                dim);
}

template <typename T>
void slice(const_tensor_view<T> A,
           const_tensor_view<T>& A0, const_tensor_view<T>& a1, const_tensor_view<T>& A2,
           unsigned dim, idx_type off)
{
    assert(&A0 != &a1);
    assert(&A0 != &A2);
    assert(dim < A.dimension());
    assert(off >= 0 && off < A.length(dim));

    std::vector<idx_type> len = A.lengths();
    std::vector<idx_type> stride = A.strides();

    len[dim] -= off+1;
    A2.reset(len, A.data()+(off+1)*stride[dim], stride);

    len[dim] = off;
    A0.reset(len, A.data(), stride);

    len.erase(len.begin()+dim);
    stride.erase(stride.begin()+dim);
    a1.reset(len, A.data()+off*stride[dim], stride);
}

template <typename T>
void slice(tensor_view<T> A,
           tensor_view<T>& A0, tensor_view<T>& a1, tensor_view<T>& A2,
           unsigned dim, idx_type off)
{
    slice(A,
          reinterpret_cast<const_tensor_view<T>&>(A0),
          reinterpret_cast<const_tensor_view<T>&>(a1),
          reinterpret_cast<const_tensor_view<T>&>(A2),
          dim, off);
}

template <typename T>
void slice_front(const_tensor_view<T> A,
                 const_tensor_view<T>& a0, const_tensor_view<T>& A1,
                 unsigned dim)
{
    assert(&a0 != &A1);
    assert(dim < A.dimension());

    std::vector<idx_type> len = A.lengths();
    std::vector<idx_type> stride = A.strides();

    len[dim]--;
    A1.reset(len, A.data()+stride[dim], stride);

    len.erase(len.begin()+dim);
    stride.erase(stride.begin()+dim);
    a0.reset(len, A.data(), stride);
}

template <typename T>
void slice_front(tensor_view<T> A,
                 tensor_view<T>& a0, tensor_view<T>& A1,
                 unsigned dim)
{
    slice_front(A,
                reinterpret_cast<const_tensor_view<T>&>(a0),
                reinterpret_cast<const_tensor_view<T>&>(A1),
                dim);
}

template <typename T>
void slice_back(const_tensor_view<T> A,
                const_tensor_view<T>& A0, const_tensor_view<T>& a1,
                unsigned dim)
{
    assert(&A0 != &a1);
    assert(dim < A.dimension());

    std::vector<idx_type> len = A.lengths();
    std::vector<idx_type> stride = A.strides();

    len[dim]--;
    A0.reset(len, A.data(), stride);

    len.erase(len.begin()+dim);
    stride.erase(stride.begin()+dim);
    a1.reset(len, A.data()+(A.length(dim)-1)*stride[dim], stride);
}

template <typename T>
void slice_back(tensor_view<T> A,
                tensor_view<T>& A0, tensor_view<T>& a1,
                unsigned dim)
{
    slice_back(A,
               reinterpret_cast<const_tensor_view<T>&>(A0),
               reinterpret_cast<const_tensor_view<T>&>(a1),
               dim);
}

template <typename T>
void unslice(const_tensor_view<T> A0, const_tensor_view<T> a1, const_tensor_view<T> A2,
             const_tensor_view<T>& A,
             unsigned dim)
{
    assert(dim < A0.dimension());
    assert(A0.dimension() == a1.dimension()+1);
    assert(A2.dimension() == a1.dimension()+1);
    assert(detail::are_congruent_along(A0, a1, dim));
    assert(detail::are_congruent_along(A0, A2, dim));
    assert(a1.data() == A0.data()+A0.length(dim)*A0.stride(dim));
    assert(A2.data() == A0.data()+(A0.length(dim)+1)*A0.stride(dim));

    std::vector<idx_type> len = A0.lengths();
    len[dim] += A2.length(dim)+1;
    A.reset(len, A0.data(), A0.strides());
}

template <typename T>
void unslice(tensor_view<T> A0, tensor_view<T> a1, tensor_view<T> A2,
             tensor_view<T>& A,
             unsigned dim)
{
    unslice(A0, a1, A2,
            reinterpret_cast<const_tensor_view<T>&>(A),
            dim);
}

template <typename T>
void unslice_front(const_tensor_view<T> a0, const_tensor_view<T> A1,
                   const_tensor_view<T>& A,
                   unsigned dim)
{
    assert(dim < A1.dimension());
    assert(A1.dimension() == a0.dimension()+1);
    assert(detail::are_congruent_along(a0, A1, dim));
    assert(A1.data() == a0.data()+A1.stride(dim));

    std::vector<idx_type> len = A1.lengths();
    len[dim]++;
    A.reset(len, a0.data(), A1.strides());
}

template <typename T>
void unslice_front(tensor_view<T> a0, tensor_view<T> A1,
                   tensor_view<T>& A,
                   unsigned dim)
{
    unslice_front(a0, A1,
                  reinterpret_cast<const_tensor_view<T>&>(A),
                  dim);
}

template <typename T>
void unslice_back(const_tensor_view<T> A0, const_tensor_view<T> a1,
                  const_tensor_view<T>& A,
                  unsigned dim)
{
    assert(dim < A0.dimension());
    assert(A0.dimension() == a1.dimension()+1);
    assert(detail::are_congruent_along(A0, a1, dim));
    assert(a1.data() == A0.data()+A0.length(dim)*A0.stride(dim));

    std::vector<idx_type> len = A0.lengths();
    len[dim]++;
    A.reset(len, A0.data(), A0.strides());
}

template <typename T>
void unslice_back(tensor_view<T> A0, tensor_view<T> a1,
                  tensor_view<T>& A,
                  unsigned dim)
{
    unslice_back(A0, a1,
                 reinterpret_cast<const_tensor_view<T>&>(A),
                 dim);
}

inline void fold(std::vector<idx_type>& lengths,
                 const std::vector<std::vector<stride_type>*>& strides,
                 std::string& idx)
{
    unsigned ndim = lengths.size();
    std::vector<unsigned> inds = MArray::range(ndim);
    stl_ext::sort(inds, detail::sort_by_stride(*strides[0]));

    std::string oldidx;
    std::vector<idx_type> oldlengths;
    std::vector<std::vector<stride_type>> oldstrides(strides.size());

    oldidx.swap(idx);
    oldlengths.swap(lengths);
    for (size_t i = 0;i < strides.size();i++) oldstrides[i].swap(*strides[i]);

    for (unsigned i = 0;i < ndim;i++)
    {
        if (i == 0) goto nofold;
        for (size_t j = 0;j < strides.size();j++)
            if (oldstrides[j][inds[i]] != oldstrides[j][inds[i-1]]*
                                          oldlengths[inds[i-1]])
                goto nofold;

        lengths.back() *= oldlengths[inds[i]];
        continue;

        nofold:
        idx.push_back(oldidx[inds[i]]);
        lengths.push_back(oldlengths[inds[i]]);
        for (size_t j = 0;j < strides.size();j++)
            strides[j]->push_back(oldstrides[j][inds[i]]);
    }

    for (size_t i = 0;i < strides.size();i++)
    {
        assert(detail::are_compatible(oldlengths, oldstrides[i],
                                      lengths, *strides[i]));
    }
}

template <typename T>
void fold(const_tensor_view<T>& AF, std::string& idx_AF)
{
    assert(AF.dimension() == idx_AF.size());

    auto len = AF.lengths();
    auto stride = AF.strides();

    fold(len, {&stride}, idx_AF);

    AF.reset(len, AF.data(), stride);
}

template <typename T>
void fold(tensor_view<T>& AF, std::string& idx_AF)
{
    fold(reinterpret_cast<const_tensor_view<T>&>(AF), idx_AF);
}

template <typename T>
void fold(const_tensor_view<T>& AF, std::string& idx_AF,
          const_tensor_view<T>& BF, std::string& idx_BF)
{
    assert(AF.dimension() == idx_AF.size());
    assert(BF.dimension() == idx_BF.size());

    using stl_ext::intersection;
    using stl_ext::exclusion;
    using stl_ext::select_from;

    auto idx_AB = intersection(idx_AF, idx_BF);
    auto len_AB = select_from(AF.lengths(), idx_AF, idx_AB);
    auto stride_A_AB = select_from(AF.strides(), idx_AF, idx_AB);
    auto stride_B_AB = select_from(BF.strides(), idx_BF, idx_AB);

    auto idx_A_only = exclusion(idx_AF, idx_AB);
    auto len_A = select_from(AF.lengths(), idx_AF, idx_A_only);
    auto stride_A_A = select_from(AF.strides(), idx_AF, idx_A_only);

    auto idx_B_only = exclusion(idx_BF, idx_AB);
    auto len_B = select_from(BF.lengths(), idx_BF, idx_B_only);
    auto stride_B_B = select_from(BF.strides(), idx_BF, idx_B_only);

    fold(len_A, {&stride_A_A}, idx_A_only);
    fold(len_B, {&stride_B_B}, idx_B_only);
    fold(len_AB, {&stride_A_AB, &stride_B_AB}, idx_AB);

    AF.reset(len_A+len_AB, AF.data(), stride_A_A+stride_A_AB);
    BF.reset(len_B+len_AB, BF.data(), stride_B_B+stride_B_AB);
    idx_AF = idx_A_only+idx_AB;
    idx_BF = idx_B_only+idx_AB;
}

template <typename T>
void fold(const_tensor_view<T>& AF, std::string& idx_AF,
                tensor_view<T>& BF, std::string& idx_BF)
{
    fold(AF, idx_AF,
         reinterpret_cast<const_tensor_view<T>&>(BF), idx_BF);
}

template <typename T>
void fold(const_tensor_view<T>& AF, std::string& idx_AF,
          const_tensor_view<T>& BF, std::string& idx_BF,
                tensor_view<T>& CF, std::string& idx_CF)
{
    assert(AF.dimension() == idx_AF.size());
    assert(BF.dimension() == idx_BF.size());
    assert(CF.dimension() == idx_CF.size());

    using stl_ext::intersection;
    using stl_ext::exclusion;
    using stl_ext::select_from;

    auto idx_ABC = intersection(idx_AF, idx_BF, idx_CF);
    auto len_ABC = select_from(AF.lengths(), idx_AF, idx_ABC);
    auto stride_A_ABC = select_from(AF.strides(), idx_AF, idx_ABC);
    auto stride_B_ABC = select_from(BF.strides(), idx_BF, idx_ABC);
    auto stride_C_ABC = select_from(CF.strides(), idx_CF, idx_ABC);

    auto idx_AB = exclusion(intersection(idx_AF, idx_BF), idx_ABC);
    auto len_AB = select_from(AF.lengths(), idx_AF, idx_AB);
    auto stride_A_AB = select_from(AF.strides(), idx_AF, idx_AB);
    auto stride_B_AB = select_from(BF.strides(), idx_BF, idx_AB);

    auto idx_AC = exclusion(intersection(idx_AF, idx_CF), idx_ABC);
    auto len_AC = select_from(AF.lengths(), idx_AF, idx_AC);
    auto stride_A_AC = select_from(AF.strides(), idx_AF, idx_AC);
    auto stride_C_AC = select_from(CF.strides(), idx_CF, idx_AC);

    auto idx_BC = exclusion(intersection(idx_BF, idx_CF), idx_ABC);
    auto len_BC = select_from(BF.lengths(), idx_BF, idx_BC);
    auto stride_B_BC = select_from(BF.strides(), idx_BF, idx_BC);
    auto stride_C_BC = select_from(CF.strides(), idx_CF, idx_BC);

    auto idx_A_only = exclusion(idx_AF, idx_BF, idx_CF);
    auto len_A = select_from(AF.lengths(), idx_AF, idx_A_only);
    auto stride_A_A = select_from(AF.strides(), idx_AF, idx_A_only);

    auto idx_B_only = exclusion(idx_BF, idx_AF, idx_CF);
    auto len_B = select_from(BF.lengths(), idx_BF, idx_B_only);
    auto stride_B_B = select_from(BF.strides(), idx_BF, idx_B_only);

    auto idx_C_only = exclusion(idx_CF, idx_AF, idx_BF);
    auto len_C = select_from(CF.lengths(), idx_CF, idx_C_only);
    auto stride_C_C = select_from(CF.strides(), idx_CF, idx_C_only);

    fold(  len_A, {                                &stride_A_A}, idx_A_only);
    fold(  len_B, {                                &stride_B_B}, idx_B_only);
    fold(  len_C, {                                &stride_C_C}, idx_C_only);
    fold( len_AB, {                &stride_A_AB,  &stride_B_AB},     idx_AB);
    fold( len_AC, {                &stride_A_AC,  &stride_C_AC},     idx_AC);
    fold( len_BC, {                &stride_B_BC,  &stride_C_BC},     idx_BC);
    fold(len_ABC, {&stride_A_ABC, &stride_B_ABC, &stride_C_ABC},    idx_ABC);

    AF.reset(len_A+len_AB+len_AC+len_ABC, AF.data(), stride_A_A+stride_A_AB+stride_A_AC+stride_A_ABC);
    BF.reset(len_B+len_AB+len_BC+len_ABC, BF.data(), stride_B_B+stride_B_AB+stride_B_BC+stride_B_ABC);
    CF.reset(len_C+len_AC+len_BC+len_ABC, CF.data(), stride_C_C+stride_C_AC+stride_C_BC+stride_C_ABC);
    idx_AF = idx_A_only+idx_AB+idx_AC+idx_ABC;
    idx_BF = idx_B_only+idx_AB+idx_BC+idx_ABC;
    idx_CF = idx_C_only+idx_AC+idx_BC+idx_ABC;
}

template <typename T>
void matricize(const_tensor_view<T>  A,
               const_matrix_view<T>& AM, unsigned split)
{
    unsigned ndim = A.dimension();
    assert(split <= ndim);
    if (ndim > 0 && A.stride(0) < A.stride(ndim-1))
    {
        for (unsigned i = 1;i < split;i++)
            assert(A.stride(i) == A.stride(i-1)*A.length(i-1));
        for (unsigned i = split+1;i < ndim;i++)
            assert(A.stride(i) == A.stride(i-1)*A.length(i-1));
    }
    else
    {
        for (unsigned i = 0;i+1 < split;i++)
            assert(A.stride(i) == A.stride(i+1)*A.length(i+1));
        for (unsigned i = split;i+1 < ndim;i++)
            assert(A.stride(i) == A.stride(i+1)*A.length(i+1));
    }

    idx_type m = 1;
    for (unsigned i = 0;i < split;i++)
    {
        m *= A.length(i);
    }

    idx_type n = 1;
    for (unsigned i = split;i < ndim;i++)
    {
        n *= A.length(i);
    }

    stride_type rs, cs;

    if (ndim == 0)
    {
        rs = cs = 1;
    }
    else if (m == 1)
    {
        rs = n;
        cs = 1;
    }
    else if (n == 1)
    {
        rs = 1;
        cs = m;
    }
    else if (A.stride(0) < A.stride(ndim-1))
    {
        rs = (split ==    0 ? 1 : A.stride(    0));
        cs = (split == ndim ? m : A.stride(split));
    }
    else
    {
        rs = (split ==    0 ? n : A.stride(split-1));
        cs = (split == ndim ? 1 : A.stride( ndim-1));
    }

    AM.reset({m, n}, A.data(), {rs, cs});
}

template <typename T>
void matricize(tensor_view<T>  A,
               matrix_view<T>& AM, unsigned split)
{
    matricize<T>(A, reinterpret_cast<const_matrix_view<T>&>(AM), split);
}

/*******************************************************************************
 *
 * Multiply two tensors together and sum onto a third
 *
 * This form generalizes contraction and weighting with the unary operations
 * trace, transpose, and replicate. Note that the binary contraction operation
 * is similar in form to the unary trace operation, while the binary weighting
 * operation is similar in form to the unary diagonal operation. Any combination
 * of these operations may be performed.
 *
 ******************************************************************************/

template <typename T>
int tensor_mult(T alpha, const_tensor_view<T> A, std::string idx_A,
                         const_tensor_view<T> B, std::string idx_B,
                T  beta,       tensor_view<T> C, std::string idx_C)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B, C, idx_C,
                         true, true, true,
                         true, true, true,
                         true);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    diagonal(C, idx_C);
    fold(A, idx_A,
         B, idx_B,
         C, idx_C);

    return impl::tensor_mult_impl(alpha, A, idx_A,
                                         B, idx_B,
                                   beta, C, idx_C);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx,
                      typename C_ptr, typename C_len, typename C_stride, typename C_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx,
                                  C_ptr, C_len, C_stride, C_idx, int>
tensor_mult(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
                     const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B,
            T  beta,       C_ptr& C, unsigned ndim_C, const C_len& len_C, const C_stride& stride_C, const C_idx& idx_C)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    const_tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));
          tensor_view<T> C_(make_len(ndim_C, len_C), make_ptr(C), make_stride(ndim_C, stride_C));

    return tensor_mult(alpha, A_, make_idx(ndim_A, idx_A),
                              B_, make_idx(ndim_B, idx_B),
                        beta, C_, make_idx(ndim_C, idx_C));
}

/*******************************************************************************
 *
 * Contract two tensors into a third
 *
 * The general form for a contraction is ab...ef... * ef...cd... -> ab...cd...
 * where the indices ef... will be summed over. Indices may be transposed in any
 * tensor. Any index group may be empty (in the case that ef... is empty, this
 * reduces to an outer product).
 *
 ******************************************************************************/

template <typename T>
int tensor_contract(T alpha, const_tensor_view<T> A, std::string idx_A,
                             const_tensor_view<T> B, std::string idx_B,
                    T  beta,       tensor_view<T> C, std::string idx_C)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B, C, idx_C,
                         false, false, false,
                         true, true, true,
                         false);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    diagonal(C, idx_C);
    fold(A, idx_A,
         B, idx_B,
         C, idx_C);

    return impl::tensor_contract_impl(alpha, A, idx_A,
                                             B, idx_B,
                                       beta, C, idx_C);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx,
                      typename C_ptr, typename C_len, typename C_stride, typename C_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx,
                                  C_ptr, C_len, C_stride, C_idx, int>
tensor_contract(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
                         const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B,
                T  beta,       C_ptr& C, unsigned ndim_C, const C_len& len_C, const C_stride& stride_C, const C_idx& idx_C)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    const_tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));
          tensor_view<T> C_(make_len(ndim_C, len_C), make_ptr(C), make_stride(ndim_C, stride_C));

    return tensor_contract(alpha, A_, make_idx(ndim_A, idx_A),
                                  B_, make_idx(ndim_B, idx_B),
                            beta, C_, make_idx(ndim_C, idx_C));
}

/*******************************************************************************
 *
 * Weight a tensor by a second and sum onto a third
 *
 * The general form for a weighting is ab...ef... * ef...cd... -> ab...cd...ef...
 * with no indices being summed over. Indices may be transposed in any tensor.
 * Any index group may be empty (in the case that ef... is empty, this reduces
 * to an outer product).
 *
 ******************************************************************************/

template <typename T>
int tensor_weight(T alpha, const_tensor_view<T> A, std::string idx_A,
                           const_tensor_view<T> B, std::string idx_B,
                  T  beta,       tensor_view<T> C, std::string idx_C)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B, C, idx_C,
                         false, false, false,
                         false, true, true,
                         true);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    diagonal(C, idx_C);
    fold(A, idx_A,
         B, idx_B,
         C, idx_C);

    return impl::tensor_weight_impl(alpha, A, idx_A,
                                           B, idx_B,
                                     beta, C, idx_C);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx,
                      typename C_ptr, typename C_len, typename C_stride, typename C_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx,
                                  C_ptr, C_len, C_stride, C_idx, int>
tensor_weight(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
                       const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B,
              T  beta,       C_ptr& C, unsigned ndim_C, const C_len& len_C, const C_stride& stride_C, const C_idx& idx_C)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    const_tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));
          tensor_view<T> C_(make_len(ndim_C, len_C), make_ptr(C), make_stride(ndim_C, stride_C));

    return tensor_weight(alpha, A_, make_idx(ndim_A, idx_A),
                                B_, make_idx(ndim_B, idx_B),
                          beta, C_, make_idx(ndim_C, idx_C));
}

/*******************************************************************************
 *
 * Sum the outer product of two tensors onto a third
 *
 * The general form for an outer product is ab... * cd... -> ab...cd... with no
 * indices being summed over. Indices may be transposed in any tensor.
 *
 ******************************************************************************/

template <typename T>
int tensor_outer_prod(T alpha, const_tensor_view<T> A, std::string idx_A,
                               const_tensor_view<T> B, std::string idx_B,
                      T  beta,       tensor_view<T> C, std::string idx_C)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B, C, idx_C,
                         false, false, false,
                         false, true, true,
                         false);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    diagonal(C, idx_C);
    fold(A, idx_A,
         B, idx_B,
         C, idx_C);

    return impl::tensor_outer_prod_impl(alpha, A, idx_A,
                                               B, idx_B,
                                         beta, C, idx_C);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx,
                      typename C_ptr, typename C_len, typename C_stride, typename C_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx,
                                  C_ptr, C_len, C_stride, C_idx, int>
tensor_outer_prod(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
                           const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B,
                  T  beta,       C_ptr& C, unsigned ndim_C, const C_len& len_C, const C_stride& stride_C, const C_idx& idx_C)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    const_tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));
          tensor_view<T> C_(make_len(ndim_C, len_C), make_ptr(C), make_stride(ndim_C, stride_C));

    return tensor_outer_prod(alpha, A_, make_idx(ndim_A, idx_A),
                                    B_, make_idx(ndim_B, idx_B),
                              beta, C_, make_idx(ndim_C, idx_C));
}

/*******************************************************************************
 *
 * Sum a tensor (presumably operated on in one or more ways) onto a second
 *
 * This form generalizes all of the unary operations trace, transpose, and
 * replicate, which may be performed in any combination.
 *
 ******************************************************************************/

template <typename T>
int tensor_sum(T alpha, const_tensor_view<T> A, std::string idx_A,
               T  beta,       tensor_view<T> B, std::string idx_B)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B,
                         true, true, true);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    fold(A, idx_A,
         B, idx_B);

    return impl::tensor_sum_impl(alpha, A, idx_A,
                                  beta, B, idx_B);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx, int>
tensor_sum(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
           T  beta, const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
          tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));

    return tensor_sum(alpha, A_, make_idx(ndim_A, idx_A),
                       beta, B_, make_idx(ndim_B, idx_B));
}

/*******************************************************************************
 *
 * Sum over (semi)diagonal elements of a tensor and sum onto a second
 *
 * The general form for a trace operation is ab...k*l*... -> ab... where k*
 * denotes the index k appearing one or more times, etc. and where the indices
 * kl... will be summed (traced) over. Indices may be transposed, and multiple
 * appearances of the traced indices kl... need not appear together. Either set
 * of indices may be empty, with the special case that when no indices are
 * traced over, the result is the same as transpose.
 *
 ******************************************************************************/

template <typename T>
int tensor_trace(T alpha, const_tensor_view<T> A, std::string idx_A,
                 T  beta,       tensor_view<T> B, std::string idx_B)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B,
                         true, false, true);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    fold(A, idx_A,
         B, idx_B);

    return impl::tensor_trace_impl(alpha, A, idx_A,
                                    beta, B, idx_B);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx, int>
tensor_trace(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
             T  beta, const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
          tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));

    return tensor_trace(alpha, A_, make_idx(ndim_A, idx_A),
                         beta, B_, make_idx(ndim_B, idx_B));
}

/*******************************************************************************
 *
 * Replicate a tensor and sum onto a second
 *
 * The general form for a replication operation is ab... -> ab...c*d*... where
 * c* denotes the index c appearing one or more times. Any indices may be
 * transposed.
 *
 ******************************************************************************/

template <typename T>
int tensor_replicate(T alpha, const_tensor_view<T> A, std::string idx_A,
                     T  beta,       tensor_view<T> B, std::string idx_B)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B,
                         false, true, true);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    fold(A, idx_A,
         B, idx_B);

    return impl::tensor_replicate_impl(alpha, A, idx_A,
                                        beta, B, idx_B);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx, int>
tensor_replicate(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
                 T  beta, const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
          tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));

    return tensor_replicate(alpha, A_, make_idx(ndim_A, idx_A),
                             beta, B_, make_idx(ndim_B, idx_B));
}

/*******************************************************************************
 *
 * Transpose a tensor and sum onto a second
 *
 * The general form for a transposition operation is ab... -> P(ab...) where P
 * is some permutation. Transposition may change the order in which the elements
 * of the tensor are physically stored.
 *
 ******************************************************************************/

template <typename T>
int tensor_transpose(T alpha, const_tensor_view<T> A, std::string idx_A,
                     T  beta,       tensor_view<T> B, std::string idx_B)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B,
                         false, false, true);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    fold(A, idx_A,
         B, idx_B);

    return impl::tensor_transpose_impl(alpha, A, idx_A,
                                        beta, B, idx_B);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx, int>
tensor_transpose(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
                 T  beta, const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
          tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));

    return tensor_transpose(alpha, A_, make_idx(ndim_A, idx_A),
                             beta, B_, make_idx(ndim_B, idx_B));
}

/*******************************************************************************
 *
 * Return the dot product of two tensors
 *
 ******************************************************************************/

template <typename T>
T tensor_dot(const_tensor_view<T> A, std::string idx_A,
             const_tensor_view<T> B, std::string idx_B)
{
    T val;
    int ret = tensor_dot(A, idx_A, B, idx_B, val);
    return val;
}

template <typename T>
int tensor_dot(const_tensor_view<T> A, std::string idx_A,
               const_tensor_view<T> B, std::string idx_B, T& val)
{
    using namespace detail;
    check_tensor_indices(A, idx_A, B, idx_B,
                         false, false, true);

    diagonal(A, idx_A);
    diagonal(B, idx_B);
    fold(A, idx_A,
         B, idx_B);

    return impl::tensor_dot_impl(A, idx_A,
                                 B, idx_B, val);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx, T>
tensor_dot(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
           T  beta, const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    const_tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));

    return tensor_dot(A_, make_idx(ndim_A, idx_A),
                      B_, make_idx(ndim_B, idx_B));
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx,
                      typename B_ptr, typename B_len, typename B_stride, typename B_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx,
                                  B_ptr, B_len, B_stride, B_idx, int>
tensor_dot(T alpha, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
           T  beta, const B_ptr& B, unsigned ndim_B, const B_len& len_B, const B_stride& stride_B, const B_idx& idx_B,
           T& val)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    const_tensor_view<T> B_(make_len(ndim_B, len_B), make_ptr(B), make_stride(ndim_B, stride_B));

    return tensor_dot(A_, make_idx(ndim_A, idx_A),
                      B_, make_idx(ndim_B, idx_B), val);
}

/*******************************************************************************
 *
 * Scale a tensor by a scalar
 *
 ******************************************************************************/

template <typename T>
int tensor_scale(T alpha, tensor_view<T> A, std::string idx_A)
{
    using namespace detail;
    check_tensor_indices(A, idx_A);

    diagonal(A, idx_A);
    fold(A, idx_A);

    return impl::tensor_scale_impl(alpha, A, idx_A);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx, int>
tensor_scale(T alpha, A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A)
{
    tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    return tensor_scale(alpha, A_, make_idx(ndim_A, idx_A));
}

/*******************************************************************************
 *
 * Return the reduction of a tensor, along with the corresponding index (as an
 * offset from A) for MAX, MIN, MAX_ABS, and MIN_ABS reductions
 *
 ******************************************************************************/

template <typename T>
std::pair<T,stride_type> tensor_reduce(reduce_t op, const_tensor_view<T> A, std::string idx_A)
{
    std::pair<T,stride_type> p;
    int ret = tensor_reduce(op, A, idx_A, p.first, p.second);
    return p;
}

template <typename T>
T tensor_reduce(reduce_t op, const_tensor_view<T> A, std::string idx_A, stride_type& idx)
{
    T val;
    int ret = tensor_reduce(op, A, idx_A, val, idx);
    return val;
}

template <typename T>
int tensor_reduce(reduce_t op, const_tensor_view<T> A, std::string idx_A, T& val)
{
   stride_type idx;
   return tensor_reduce(op, A, idx_A, val, idx);
}

template <typename T>
int tensor_reduce(reduce_t op, const_tensor_view<T> A, std::string idx_A, T& val, stride_type& idx)
{
    using namespace detail;
    check_tensor_indices(A, idx_A);

    diagonal(A, idx_A);
    fold(A, idx_A);

    return impl::tensor_reduce_impl(op, A, idx_A, val, idx);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx, std::pair<T,stride_type>>
tensor_reduce(reduce_t op, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    return tensor_reduce(op, A_, make_idx(ndim_A, idx_A));
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx, int>
tensor_reduce(reduce_t op, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
              stride_type& idx)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    return tensor_reduce(op, A_, make_idx(ndim_A, idx_A), idx);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx, int>
tensor_reduce(reduce_t op, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
              T& val)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    return tensor_reduce(op, A_, make_idx(ndim_A, idx_A), val);
}

template <typename T, typename A_ptr, typename A_len, typename A_stride, typename A_idx>
detail::check_template_types_t<T, A_ptr, A_len, A_stride, A_idx, int>
tensor_reduce(reduce_t op, const A_ptr& A, unsigned ndim_A, const A_len& len_A, const A_stride& stride_A, const A_idx& idx_A,
              T& val, stride_type& idx)
{
    const_tensor_view<T> A_(make_len(ndim_A, len_A), make_ptr(A), make_stride(ndim_A, stride_A));
    return tensor_reduce(op, A_, make_idx(ndim_A, idx_A), val, idx);
}

/*******************************************************************************
 *
 * Storage size helper functions.
 *
 ******************************************************************************/

template <typename len_type>
stl_ext::enable_if_t<std::is_integral<detail::pointer_type_t<len_type>>::value,size_t>
tensor_size(unsigned ndim, const len_type& len)
{
    size_t size = 1;

    for (unsigned i = 0;i < ndim;i++)
    {
        size *= len[i];
    }

    return size;
}

template <typename len_type, typename stride_type>
stl_ext::enable_if_t<std::is_integral<detail::pointer_type_t<len_type>>::value &&
                     std::is_integral<detail::pointer_type_t<stride_type>>::value,size_t>
tensor_storage_size(unsigned ndim, const len_type& len, const stride_type& stride)
{
    if (!stride)
    {
       return tensor_size(ndim, len);
    }

    size_t size = 1;

    for (unsigned i = 0;i < ndim;i++)
    {
        size += std::abs(stride[i])*(len[i]-1);
    }

    return size;
}

}

#endif
