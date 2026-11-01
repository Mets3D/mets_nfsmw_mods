// A view's camera movers are an intrusive circular list whose head node (next, prev) sits in the view (+0x44), each mover's node at its +4. The game asks the view's
// first mover which car it follows and whether that car's inside is seen (bumper view: not; the rear view mirror: the car is not drawn in it), so the list is part of what
// makes a view what it is.
#pragma once

namespace split_screen_support {

struct ListNode {
    ListNode *next, *prev;
};
constexpr int kViewMoverList = 0x44;

// The two lists trade places node by node: every node points back to its list's head.
inline void swap_lists(ListNode *a, ListNode *b) {
    const ListNode from_a = *a, from_b = *b;
    const bool a_empty = from_a.next == a, b_empty = from_b.next == b;
    if (b_empty) {
        a->next = a->prev = a;
    } else {
        *a = from_b;
        a->next->prev = a;
        a->prev->next = a;
    }
    if (a_empty) {
        b->next = b->prev = b;
    } else {
        *b = from_a;
        b->next->prev = b;
        b->prev->next = b;
    }
}

}  // namespace split_screen_support
